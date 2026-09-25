#pragma once
#include <sdl-rdp/utilities/scoped.hpp>

#include <SDL3/SDL_video.h>
#include <functional>
#include <memory>

namespace sdl_rdp::sample_gate_test::process::detail::initialized_sdl {
using sdl_rdp::utilities::RAIIWrap;

auto InitializeSdl(std::function<bool()> const& initialize) -> bool;
auto QuitSdl(bool initialized) noexcept                     -> void;
// SDL_Quit runs even when an assertion returns early.
using InitializedSdl = RAIIWrap<bool, InitializeSdl, QuitSdl>;
using Window         = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
}

namespace sdl_rdp::sample_gate_test::process {
using detail::initialized_sdl::InitializedSdl;
using detail::initialized_sdl::Window;
}
