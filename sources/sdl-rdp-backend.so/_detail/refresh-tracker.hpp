#pragma once
#include "contract.hpp"
#include "refresh.hpp"

#include <atomic>
#include <concepts>

namespace Backend {
inline constexpr unsigned DefaultRefreshRate = 60;
class RefreshTracker {
public:
  unsigned    Effective() const             noexcept;
  RefreshMode Mode() const                  noexcept;
  bool        AwaitingEmpty() const         noexcept;
  bool        TestAndSetUnavailableLogged() noexcept;
  bool        Adjust(std::invocable<Refresh&> auto step) {
    auto const previous = _refresh.Rate();
    step(_refresh);
    Expects(_refresh.Rate() > 0, "effective refresh is positive");
    _effective.store(_refresh.Rate());
    return _refresh.Rate() != previous;
  }

private:
  Refresh          _refresh;
  std::atomic_uint _effective         { DefaultRefreshRate };
  bool             _unavailable_logged{ };
};
}
