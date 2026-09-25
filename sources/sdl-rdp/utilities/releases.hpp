#pragma once

namespace Backend::detail::releases {
// A deleter over a C API: each release function is called on the handle, in the order given.
template <auto... RELEASES>
  requires(sizeof...(RELEASES) > 0)
class Releases {
public:
  template <typename VTy> auto operator()(VTy* what) const noexcept -> void {
    (..., static_cast<void>(RELEASES(what)));
  }
};
}
namespace Backend {
using detail::releases::Releases;
}
