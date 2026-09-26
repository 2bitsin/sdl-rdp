#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <sdl-rdp/utilities/exceptions.hpp>

#include <freerdp/channels/wtsvc.h>
#include <winpr/synch.h>

namespace sdl_rdp::freerdp_facade::detail::rdp_handles {
using sdl_rdp::utilities::AllocationFailed;

auto ManualResetEvent(std::string_view subject) -> EventHandle {
  EventHandle event{ CreateEvent(nullptr, true, false, nullptr) };
  if (!event) throw AllocationFailed{ subject };
  return event;
}
auto ForgetChannelCreation(HANDLE manager) -> void {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, nullptr, nullptr);
}
}
