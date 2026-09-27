#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <winpr/handle.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>
#include <type_traits>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::rdp_handles {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
static_assert(std::is_same_v<HANDLE, void*>, "WinPR's HANDLE is the void* the releases take");

auto CloseEvent(void* event) noexcept -> void {
  CloseHandle(event);
}
auto CloseServer(void* server) noexcept -> void {
  WTSCloseServer(server);
}
auto CloseChannel(void* channel) noexcept -> void {
  WTSVirtualChannelClose(channel);
}
EventHandle::EventHandle(OwnedEvent event) noexcept : _event{ std::move(event) } {
  Expects(_event != nullptr, "an event handle owns an event");
}
auto EventHandle::Set() const -> void {
  auto const set = SetEvent(_event.get());
  Ensures(set != 0, "WinPR sets a live event");
}
auto EventHandle::Reset() const -> void {
  auto const reset = ResetEvent(_event.get());
  Ensures(reset != 0, "WinPR resets a live event");
}
auto ManualResetEvent(std::string_view subject) -> EventHandle {
  OwnedEvent event{ CreateEvent(nullptr, true, false, nullptr) };
  if (!event) throw AllocationFailed{ subject };
  return EventHandle{ std::move(event) };
}
}
