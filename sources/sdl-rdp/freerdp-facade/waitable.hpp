#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

namespace sdl_rdp::freerdp_facade::detail::waitable {
// A handle WinPR lends for waiting; the object it came from owns and closes it.
class Waitable {
public:
  explicit Waitable(WaitHandle handle);
  auto     Signalled() const -> bool;

private:
  WaitHandle _handle;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::waitable::Waitable;
}
