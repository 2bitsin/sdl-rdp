#pragma once
#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::utilities::detail::releases {
// A deleter over a C API: each release function is called on the handle, in the order given.
template <auto... RELEASES>
  requires(sizeof...(RELEASES) > 0)
class Releases {
public:
  template <typename VTy> auto operator()(VTy* what) const noexcept -> void {
    Expects(what != nullptr, "unique_ptr releases the pointer it holds");
    (..., static_cast<void>(RELEASES(what)));
  }
};
}

namespace sdl_rdp::utilities {
using detail::releases::Releases;
}
