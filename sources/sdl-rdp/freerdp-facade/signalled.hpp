#pragma once
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::signalled {
// The handles a wait found signalled, asked only whether one of them fired.
class Signalled {
public:
  explicit Signalled(std::span<WaitHandle const> handles);
  auto     Contains(WaitHandle handle) const                -> bool;
  auto     Contains(std::optional<WaitHandle> handle) const -> bool;

private:
  std::array<WaitHandle, MaximumWaitHandles> _fired{ };
  std::size_t                                _count{ };
};
}

namespace sdl_rdp::freerdp_facade {
using detail::signalled::Signalled;
}
