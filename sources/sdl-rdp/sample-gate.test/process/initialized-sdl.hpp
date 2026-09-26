#pragma once
#include <sdl-rdp/utilities/releases.hpp>
#include <sdl-rdp/utilities/scoped.hpp>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_storage.h>
#include <SDL3/SDL_video.h>
#include <functional>
#include <memory>

namespace sdl_rdp::sample_gate_test::process::detail::initialized_sdl {
using sdl_rdp::utilities::RAIIWrap;
using sdl_rdp::utilities::Releases;

struct StreamLock {
  std::reference_wrapper<SDL_AudioStream> stream;
  bool                                    locked;
};

auto InitializeSdl(std::function<bool()> const& initialize) -> bool;
auto QuitSdl(bool initialized) noexcept                     -> void;
auto LockStream(SDL_AudioStream& stream)                    -> StreamLock;
auto UnlockStream(StreamLock const& lock) noexcept          -> void;
// SDL_Quit runs even when an assertion returns early, and clears every hint and SDL's environment copy.
using InitializedSdl = RAIIWrap<bool, InitializeSdl, QuitSdl>;
using Window         = std::unique_ptr<SDL_Window, Releases<SDL_DestroyWindow>>;
using Renderer       = std::unique_ptr<SDL_Renderer, Releases<SDL_DestroyRenderer>>;
using Storage        = std::unique_ptr<SDL_Storage, Releases<SDL_CloseStorage>>;
using AudioStream    = std::unique_ptr<SDL_AudioStream, Releases<SDL_DestroyAudioStream>>;
using LockedStream   = RAIIWrap<StreamLock, LockStream, UnlockStream>;
}

namespace sdl_rdp::sample_gate_test::process {
using detail::initialized_sdl::AudioStream;
using detail::initialized_sdl::InitializedSdl;
using detail::initialized_sdl::LockedStream;
using detail::initialized_sdl::Renderer;
using detail::initialized_sdl::Storage;
using detail::initialized_sdl::Window;
}
