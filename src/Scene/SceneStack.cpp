#include "Emerald/Scene/SceneStack.h"

#include <utility>

#include "Emerald/Core/Log.h"
#include "Emerald/Input/Input.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

bool Scene::HasFocus() const
{
    return m_Stack && m_Stack->GetTop() == this && !m_Stack->IsTransitioning();
}

// --- Requests ---------------------------------------------------------------------------------

void SceneStack::Push(std::unique_ptr<Scene> scene, Transition transition)
{
    if (!scene) {
        EM_CORE_WARN("SceneStack: Push with no scene; ignored");
        return;
    }
    m_Pending.push_back({Kind::Push, std::move(scene), std::move(transition)});
}

void SceneStack::Pop(Transition transition)
{
    m_Pending.push_back({Kind::Pop, nullptr, std::move(transition)});
}

void SceneStack::Replace(std::unique_ptr<Scene> scene, Transition transition)
{
    if (!scene) {
        EM_CORE_WARN("SceneStack: Replace with no scene; ignored");
        return;
    }
    m_Pending.push_back({Kind::Replace, std::move(scene), std::move(transition)});
}

void SceneStack::Clear(Transition transition)
{
    m_Pending.push_back({Kind::Clear, nullptr, std::move(transition)});
}

void SceneStack::ReplaceAll(std::unique_ptr<Scene> scene, Transition transition)
{
    if (!scene) {
        EM_CORE_WARN("SceneStack: ReplaceAll with no scene; ignored");
        return;
    }
    m_Pending.push_back({Kind::ReplaceAll, std::move(scene), std::move(transition)});
}

void SceneStack::ExitAll()
{
    m_Pending.clear();
    m_Tweens.Clear();
    m_Current = {};
    m_Phase = Phase::Idle;
    m_Cover = 0.0f;
    Request clear;
    clear.What = Kind::Clear;
    Apply(clear);
    m_Pending.clear(); // anything the leaving scenes asked for
}

// The change itself, with the hooks in a fixed order: the old top hears first.
void SceneStack::Apply(Request& request)
{
    const auto popTop = [this] {
        m_Scenes.back()->OnExit();
        m_Scenes.back()->m_Stack = nullptr;
        m_Scenes.pop_back(); // destroyed here: no hook of it is running
    };
    const auto push = [this](std::unique_ptr<Scene> scene) {
        scene->m_Stack = this;
        m_Scenes.push_back(std::move(scene));
        m_Scenes.back()->OnEnter();
    };

    switch (request.What) {
    case Kind::Push:
        if (!m_Scenes.empty())
            m_Scenes.back()->OnPause();
        push(std::move(request.NewScene));
        break;
    case Kind::Pop:
        if (m_Scenes.empty()) {
            EM_CORE_WARN("SceneStack: Pop on an empty stack; ignored");
            break;
        }
        popTop();
        if (!m_Scenes.empty())
            m_Scenes.back()->OnResume();
        break;
    case Kind::Replace:
        if (!m_Scenes.empty())
            popTop(); // the scene below stays paused: it is still covered
        push(std::move(request.NewScene));
        break;
    case Kind::Clear:
    case Kind::ReplaceAll:
        while (!m_Scenes.empty())
            popTop();
        if (request.NewScene)
            push(std::move(request.NewScene));
        break;
    }
}

// Instant changes go through one after another; one with a transition starts it and waits.
void SceneStack::ApplyPending()
{
    while (m_Phase == Phase::Idle && !m_Pending.empty()) {
        Request request = std::move(m_Pending.front());
        m_Pending.pop_front();
        if (request.Look.Duration <= 0.0f) {
            Apply(request);
            continue;
        }
        m_Current = std::move(request);
        m_TweenDone = false;
        if (m_Scenes.empty()) {
            // Nothing to cover: start covered, so the first scene fades in.
            m_Phase = Phase::Uncovering;
            m_Cover = 1.0f;
            Apply(m_Current);
            m_Tweens.FromTo(
                &m_Cover, 1.0f, 0.0f, m_Current.Look.Duration,
                {.Curve = m_Current.Look.Curve, .OnComplete = [this] { m_TweenDone = true; }});
        } else {
            m_Phase = Phase::Covering;
            m_Tweens.FromTo(
                &m_Cover, 0.0f, 1.0f, m_Current.Look.Duration,
                {.Curve = m_Current.Look.Curve, .OnComplete = [this] { m_TweenDone = true; }});
        }
    }
}

// --- The loop ---------------------------------------------------------------------------------

usize SceneStack::GetFirst(bool drawn) const
{
    if (m_Scenes.empty())
        return 0;
    usize first = m_Scenes.size() - 1;
    while (first > 0 && (drawn ? m_Scenes[first]->DrawBelow : m_Scenes[first]->UpdateBelow))
        --first;
    return first;
}

template <typename Fn> void SceneStack::ForEach(usize first, Fn&& fn)
{
    // By index: a hook may queue requests, but the vector only changes in Apply.
    const bool wasBlocked = m_Input && m_Input->IsBlocked();
    for (usize i = first; i < m_Scenes.size(); ++i) {
        Scene& scene = *m_Scenes[i];
        if (m_Input)
            m_Input->SetBlocked(wasBlocked || !scene.HasFocus());
        fn(scene);
    }
    if (m_Input)
        m_Input->SetBlocked(wasBlocked);
}

void SceneStack::OnEvent(const SDL_Event& event)
{
    if (Scene* top = GetTop(); top && top->HasFocus())
        top->OnEvent(event);
}

void SceneStack::FixedUpdate(f32 dt)
{
    ForEach(GetFirst(false), [dt](Scene& s) { s.OnFixedUpdate(dt); });
}

void SceneStack::Update(f32 dt)
{
    ForEach(GetFirst(false), [dt](Scene& s) { s.OnUpdate(dt); });

    // The transition: covered -> make the change and uncover; uncovered -> done.
    m_Tweens.Update(dt);
    if (m_TweenDone && m_Phase == Phase::Covering) {
        m_TweenDone = false;
        m_Phase = Phase::Uncovering;
        Apply(m_Current);
        m_Tweens.FromTo(
            &m_Cover, 1.0f, 0.0f, m_Current.Look.Duration,
            {.Curve = m_Current.Look.Curve, .OnComplete = [this] { m_TweenDone = true; }});
    } else if (m_TweenDone && m_Phase == Phase::Uncovering) {
        m_TweenDone = false;
        m_Phase = Phase::Idle;
        m_Cover = 0.0f;
        m_Current = {};
    }
    ApplyPending();
}

void SceneStack::Render(SDL_GPURenderPass* pass)
{
    ForEach(GetFirst(true), [pass](Scene& s) { s.OnRender(pass); });
}

void SceneStack::Render2D(Renderer2D& r, Vec2 viewSize)
{
    ForEach(GetFirst(true), [&r](Scene& s) { s.OnRender2D(r); });
    if (!IsTransitioning())
        return;
    r.Begin(Mat4::OrthoPixelSpace(viewSize.x, viewSize.y));
    const Transition& look = m_Current.Look;
    if (look.Draw)
        look.Draw(r, viewSize, m_Cover);
    else
        r.FillRect({0.0f, 0.0f}, viewSize, {look.Color.x, look.Color.y, look.Color.z, m_Cover});
    r.End();
}

void SceneStack::ImGui()
{
    ForEach(0, [](Scene& s) { s.OnImGui(); });
}

} // namespace Emerald
