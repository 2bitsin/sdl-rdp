#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

namespace sdl_rdp::freerdp_facade::detail::waitable {
// A handle WinPR lends for waiting; the object it came from owns and closes it.
class Waitable {
public:
  explicit Waitable(Backend::WaitHandle handle);
  auto     Signalled() const -> bool;

private:
  Backend::WaitHandle _handle;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::waitable::Waitable;
}
