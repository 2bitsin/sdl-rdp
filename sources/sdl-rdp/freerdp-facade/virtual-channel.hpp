#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <cstddef>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::virtual_channel {
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::freerdp_facade::detail::rdp_handles::ChannelHandle;

// One opened static virtual channel: whole messages in, bytes out, closed with it.
class VirtualChannel {
public:
  explicit VirtualChannel(ChannelHandle opened);
  auto     Write(std::span<std::byte const> bytes) -> bool;
  auto     Pending() const                         -> std::optional<std::size_t>;
  auto     Read(std::span<std::byte> into)         -> std::optional<std::size_t>;
  auto     Handle() const                          -> WaitHandle;

private:
  auto Channel() const -> ChannelHandle const&;
  ChannelHandle _handle;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::virtual_channel::VirtualChannel;
}
