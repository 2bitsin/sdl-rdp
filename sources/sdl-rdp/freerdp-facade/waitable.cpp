#include <sdl-rdp/freerdp-facade/waitable.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <winpr/synch.h>

namespace sdl_rdp::freerdp_facade::detail::waitable {
Waitable::Waitable(Backend::WaitHandle handle) : _handle{ handle } {
  ::utilities::Expects(handle != nullptr, "a waitable handle exists");
  ::utilities::Expects(handle != INVALID_HANDLE_VALUE, "a waitable handle is valid");
}
auto Waitable::Signalled() const -> bool {
  auto const result = WaitForSingleObject(_handle, 0);
  if (result == WAIT_FAILED) throw Backend::EventWaitFailed{ };
  return result == WAIT_OBJECT_0;
}
}
