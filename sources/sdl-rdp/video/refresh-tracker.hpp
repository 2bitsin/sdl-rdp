#pragma once
#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <atomic>
#include <concepts>
#include <cstdint>

namespace Backend {
inline constexpr std::uint32_t DefaultRefreshRate = 60;
class RefreshTracker {
public:
  auto Effective() const noexcept                 -> std::uint32_t;
  auto Mode() const noexcept                      -> RefreshMode;
  auto AwaitingEmpty() const noexcept             -> bool;
  auto TestAndSetUnavailableLogged() noexcept     -> bool;
  auto Adjust(std::invocable<Refresh&> auto step) -> bool {
    auto const previous = _refresh.Rate();
    step(_refresh);
    Expects(_refresh.Rate() > 0, "effective refresh is positive");
    _effective.store(_refresh.Rate());
    return _refresh.Rate() != previous;
  }

private:
  Refresh                    _refresh;
  std::atomic<std::uint32_t> _effective         { DefaultRefreshRate };
  bool                       _unavailable_logged{ };
};
}
