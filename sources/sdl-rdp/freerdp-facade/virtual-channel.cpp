#include <sdl-rdp/freerdp-facade/virtual-channel.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/parameter.hpp>

#include <oxbox/utilities/span.hpp>
#include <winpr/wtsapi.h>
#include <type_traits>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::virtual_channel {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Parameter;

namespace {
// abi: WinPR's ULONG is unsigned long under Windows and std::uint32_t elsewhere.
using Transferred = std::remove_pointer_t<Parameter<WTSVirtualChannelRead, 4>>;
}
VirtualChannel::VirtualChannel(ChannelHandle opened) : _handle{ std::move(opened) } {
  Expects(_handle != nullptr, "an opened channel exists");
}
auto VirtualChannel::Write(std::span<std::byte const> bytes) -> bool {
  auto const  text    = oxbox::utilities::SpanCast<char const>(bytes);
  Transferred written = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): WinPR's PCHAR is only read (server.c:1749), CG ES.50.
  return WTSVirtualChannelWrite(Channel().get(), const_cast<char*>(text.data()), Narrowed<Transferred>(text.size()),
                                &written)
         && written == text.size();
}
auto VirtualChannel::Pending() const -> std::optional<std::size_t> {
  Transferred length = 0;
  if (!WTSVirtualChannelRead(Channel().get(), 0, nullptr, 0, &length)) return std::nullopt;
  return length;
}
auto VirtualChannel::Read(std::span<std::byte> into) -> std::optional<std::size_t> {
  Expects(!into.empty(), "a read has room, an empty one is Pending");
  // Complain mode: an empty read delivers nothing, not the pending length WinPR would report.
  if (into.empty()) return 0;
  auto const  text = oxbox::utilities::SpanCast<char>(into);
  Transferred read = 0;
  if (!WTSVirtualChannelRead(Channel().get(), 0, text.data(), Narrowed<Transferred>(text.size()), &read))
    return std::nullopt;
  return read;
}
auto VirtualChannel::Handle() const -> WaitHandle {
  return WaitHandle::Of(Channel());
}
auto VirtualChannel::Channel() const -> ChannelHandle const& {
  Expects(_handle != nullptr, "the virtual channel is not moved from");
  return _handle;
}
}
