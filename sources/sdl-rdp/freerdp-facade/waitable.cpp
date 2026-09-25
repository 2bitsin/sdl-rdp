#include <sdl-rdp/freerdp-facade/waitable.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <winpr/synch.h>

namespace sdl_rdp::freerdp_facade::detail::waitable {
using sdl_rdp::utilities::Expects;

Waitable::Waitable(WaitHandle handle) : _handle{ handle } {
  Expects(handle != nullptr, "a waitable handle exists");
  Expects(handle != INVALID_HANDLE_VALUE, "a waitable handle is valid");
}
auto Waitable::Signalled() const -> bool {
  auto const result = WaitForSingleObject(_handle, 0);
  if (result == WAIT_FAILED) throw EventWaitFailed{ };
  return result == WAIT_OBJECT_0;
}
}
