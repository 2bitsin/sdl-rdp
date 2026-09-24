#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Headless {
using BackendHandle = std::unique_ptr<sdlrdp_handle, Backend::Releases<sdlrdp_close>>;
class BackendInstance {
public:
  auto     Open(sdlrdp_config const& config)    -> void;
  auto     TryOpen(sdlrdp_config const& config) -> int;
  auto     Close() noexcept                     -> void;
  auto     Handle() const noexcept              -> sdlrdp_handle*;
  auto     operator*() const                    -> sdlrdp_handle&;
  explicit operator bool() const noexcept;
  auto     Poll() const                         -> std::vector<sdlrdp_event>;
  auto     Present(std::span<std::uint32_t const> pixels, std::uint32_t width, std::uint32_t height,
                   sdlrdp_rect const& damage) const -> int;

private:
  BackendHandle _handle;
};
}
