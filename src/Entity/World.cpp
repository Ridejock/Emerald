#include "Emerald/Entity/World.h"

namespace Emerald {

Entity World::Spawn()
{
    ++m_Count;
    ++m_SpawnedTotal;
    return {this, m_Registry.create()};
}

void World::Destroy(Entity entity)
{
    if (entity.GetWorld() != this || !entity.IsValid())
        return;
    m_Registry.emplace<Doomed>(entity.GetId());
    m_Doomed.push_back(entity.GetId());
}

void World::Flush()
{
    if (m_Doomed.empty())
        return;
    m_Registry.destroy(m_Doomed.begin(), m_Doomed.end());
    m_Count -= m_Doomed.size();
    m_DestroyedTotal += m_Doomed.size();
    m_Doomed.clear(); // keeps its capacity for the next frame
}

void World::Clear()
{
    m_DestroyedTotal += m_Count;
    m_Registry.clear();
    m_Doomed.clear();
    m_Count = 0;
}

} // namespace Emerald
