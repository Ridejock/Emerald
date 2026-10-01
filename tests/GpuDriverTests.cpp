// --gpu parsing and driver names (no GPU needed).

#include <array>

#include <Emerald/Renderer/Renderer.h>

#include "Test.h"

using Emerald::FindGpuArg;
using Emerald::NormalizeGpuDriver;

TEST(GpuDriverNames)
{
    CHECK(NormalizeGpuDriver("vulkan") == "vulkan");
    CHECK(NormalizeGpuDriver("Vulkan") == "vulkan"); // case-insensitive
    CHECK(NormalizeGpuDriver("d3d12") == "direct3d12");
    CHECK(NormalizeGpuDriver("direct3d12") == "direct3d12");
    CHECK(NormalizeGpuDriver("metal") == "metal");
    const auto automatic = NormalizeGpuDriver("auto");
    CHECK(automatic.has_value() && automatic->empty()); // "" = let SDL pick
    CHECK(!NormalizeGpuDriver("opengl").has_value());
    CHECK(!NormalizeGpuDriver("").has_value());
}

TEST(GpuDriverArg)
{
    char exe[] = "game", gpu[] = "--gpu", vulkan[] = "vulkan", other[] = "--frames",
         number[] = "60", inline_[] = "--gpu=d3d12";
    {
        std::array<char*, 4> args{exe, other, number, nullptr};
        CHECK(!FindGpuArg({args.data(), 3}).has_value()); // no flag
    }
    {
        std::array<char*, 5> args{exe, other, number, gpu, vulkan};
        CHECK(FindGpuArg(args) == "vulkan");
    }
    {
        std::array<char*, 2> args{exe, inline_};
        CHECK(FindGpuArg(args) == "d3d12");
    }
    {
        std::array<char*, 4> args{exe, gpu, vulkan, inline_}; // the last one wins
        CHECK(FindGpuArg(args) == "d3d12");
    }
    {
        std::array<char*, 2> args{exe, gpu}; // flag without a value
        CHECK(!FindGpuArg(args).has_value());
    }
    {
        std::array<char*, 2> args{gpu, vulkan}; // args[0] is the program, not an option
        CHECK(!FindGpuArg(args).has_value());
    }
    CHECK(!FindGpuArg({}).has_value());
}
