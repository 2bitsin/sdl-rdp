#pragma once
#include <sdl-rdp/utilities/contract.hpp>

namespace Backend::detail::releases {
// A deleter over a C API: each release function is called on the handle, in the order given.
template <auto... RELEASES>
  requires(sizeof...(RELEASES) > 0)
class Releases {
public:
  template <typename VTy> auto operator()(VTy* what) const noexcept -> void {
    ::utilities::Expects(what != nullptr, "unique_ptr releases the pointer it holds");
    (..., static_cast<void>(RELEASES(what)));
  }
};
}
namespace Backend {
using detail::releases::Releases;
}
