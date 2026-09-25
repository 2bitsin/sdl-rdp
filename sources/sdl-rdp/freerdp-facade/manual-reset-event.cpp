#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>

#include <sdl-rdp/utilities/exceptions.hpp>

#include <winpr/synch.h>

namespace sdl_rdp::freerdp_facade::detail::manual_reset_event {
auto ManualResetEvent(std::string_view subject) -> ::Backend::EventHandle {
  ::Backend::EventHandle event{ CreateEvent(nullptr, true, false, nullptr) };
  if (!event) throw ::Backend::AllocationFailed{ subject };
  return event;
}
}
