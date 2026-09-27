#include <sdl-rdp/freerdp-facade/channel-manager.hpp>

#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/text.hpp>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/svc.h>
#include <winpr/wtsapi.h>
#include <array>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::channel_manager {
using sdl_rdp::utilities::CopyTerminated;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::OperationName;

namespace {
constexpr OperationName ChannelCreation{ "Dynamic channel creation" };
auto ServerOf(rdp_context& context) -> ServerHandle {
  // FreeRDP 3.32 server.c:1134 WTSOpenServerA takes the peer's rdpContext through its server-name parameter.
  auto* opened = WTSOpenServerA(reinterpret_cast<char*>(&context));
  if (!opened || opened == INVALID_HANDLE_VALUE) throw AllocationFailed{ "Channel manager" };
  return ServerHandle{ opened };
}
// FreeRDP matches a static channel name with strnlen, so it travels terminated.
auto Terminated(std::string_view name) -> std::array<char, CHANNEL_NAME_LEN + 1> {
  Expects(name.size() <= CHANNEL_NAME_LEN, "a static channel name fits its protocol field");
  std::array<char, CHANNEL_NAME_LEN + 1> terminated{ };
  CopyTerminated(terminated, name);
  return terminated;
}
}
auto ForgetChannelCreation(void* manager) noexcept -> void {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, nullptr, nullptr);
}
ChannelManager::ChannelManager(Connection& connection)
    : _session{ connection.Context() }, _handle{ ServerOf(_session) } { }
auto ChannelManager::Open(std::string_view name) -> VirtualChannel {
  auto opened = Opened(name);
  if (!opened) throw ChannelOpenFailed{ name };
  return VirtualChannel{ std::move(opened) };
}
// FreeRDP leaks the channel of a context without its own thread (2bitsin/FreeRDP#1); reopening returns it to close.
auto ChannelManager::Reclaim(std::string_view name) noexcept -> void {
  ChannelHandle const reopened = Opened(name);
}
auto ChannelManager::Joined(std::string_view name) const -> bool {
  auto terminated = Terminated(name);
  return WTSVirtualChannelManagerIsChannelJoined(_handle.get(), terminated.data());
}
auto ChannelManager::DynamicReady() const -> bool {
  return WTSVirtualChannelManagerGetDrdynvcState(_handle.get()) == DRDYNVC_STATE_READY;
}
auto ChannelManager::Pump() -> bool {
  return WTSVirtualChannelManagerCheckFileDescriptor(_handle.get());
}
// Sends the queued channel PDUs without opening the dynamic channel the way Pump does (FreeRDP 3.32 server.c:683).
auto ChannelManager::Flush() -> bool {
  return WTSVirtualChannelManagerCheckFileDescriptorEx(_handle.get(), false);
}
auto ChannelManager::Handle() const -> WaitHandle {
  return WaitHandle::Of(_handle);
}
auto ChannelManager::OnDynamicCreation(DynamicCreationSink& sink) -> CreationRegistration {
  // abi: psDVCCreationStatusCallback, BOOL is int
  WTSVirtualChannelManagerSetDVCCreationCallback(
      _handle.get(),
      Handled<Itself<DynamicCreationSink>, &DynamicCreationSink::Created, ChannelCreation, SinkFailures, false>, &sink);
  return CreationRegistration{ _handle.get() };
}
auto ChannelManager::Opened(std::string_view name) noexcept -> ChannelHandle {
  auto terminated = Terminated(name);
  return ChannelHandle{ WTSVirtualChannelOpen(_handle.get(), WTS_CURRENT_SESSION, terminated.data()) };
}
}
