#pragma once

// A deliberately tiny test harness (no dependencies):
//
//   TEST(VectorsAdd) { CHECK(Vec2(1, 2) + Vec2(3, 4) == Vec2(4, 6)); }
//
// TEST registers a function that TestMain.cpp runs; CHECK/CHECK_NEAR record a failure (with file,
// line and the expression) and keep going, so one run reports every broken check.

#include <cstdio>
#include <string>
#include <vector>

#include <Emerald/Math/Math.h>

namespace Test {

struct Case {
    const char* Name;
    void (*Function)();
};

inline std::vector<Case>& Registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline bool Register(const char* name, void (*function)())
{
    Registry().push_back({name, function});
    return true;
}

// Failures in the currently running test.
inline int& CurrentFailures()
{
    static int failures = 0;
    return failures;
}

inline void Fail(const char* file, int line, const std::string& message)
{
    std::printf("    %s:%d: %s\n", file, line, message.c_str());
    ++CurrentFailures();
}

// ToString overloads so CHECK_NEAR can print both sides.
inline std::string ToString(f32 v)
{
    return std::to_string(v);
}
inline std::string ToString(const Emerald::Vec2& v)
{
    return "(" + ToString(v.x) + ", " + ToString(v.y) + ")";
}
inline std::string ToString(const Emerald::Vec3& v)
{
    return "(" + ToString(v.x) + ", " + ToString(v.y) + ", " + ToString(v.z) + ")";
}
inline std::string ToString(const Emerald::Vec4& v)
{
    return "(" + ToString(v.x) + ", " + ToString(v.y) + ", " + ToString(v.z) + ", " +
           ToString(v.w) + ")";
}
inline std::string ToString(const Emerald::Mat4& m)
{
    std::string s = "rows [";
    for (usize r = 0; r < 4; ++r)
        s += (r ? ", " : "") + ToString(m.Row(r));
    return s + "]";
}

} // namespace Test

#define TEST(name)                                                                                 \
    static void name();                                                                            \
    [[maybe_unused]] static const bool name##Registered = ::Test::Register(#name, &name);          \
    static void name()

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond))                                                                               \
            ::Test::Fail(__FILE__, __LINE__, "CHECK(" #cond ") failed");                           \
    } while (false)

// Approximate comparison using Emerald::NearlyEqual (works for f32, Vec2/3/4 and Mat4).
#define CHECK_NEAR(a, b) CHECK_NEAR_EPS(a, b, ::Emerald::Epsilon)
#define CHECK_NEAR_EPS(a, b, epsilon)                                                              \
    do {                                                                                           \
        const auto& lhs_ = (a);                                                                    \
        const auto& rhs_ = (b);                                                                    \
        if (!::Emerald::NearlyEqual(lhs_, rhs_, (epsilon)))                                        \
            ::Test::Fail(__FILE__, __LINE__,                                                       \
                         "CHECK_NEAR(" #a ", " #b "): " + ::Test::ToString(lhs_) +                 \
                             " != " + ::Test::ToString(rhs_));                                     \
    } while (false)
