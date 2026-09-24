#pragma once
#include "lease-count.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace Headless {
template <class ObserverTy> class ObserverLease {
public:
       ObserverLease(ObserverTy& observer, LeaseCount& leases);
       ObserverLease(ObserverLease const&)               = delete;
       ObserverLease(ObserverLease&& other)              noexcept;
       ~ObserverLease();
  auto operator=(ObserverLease const&) -> ObserverLease& = delete;
  auto operator=(ObserverLease&&)      -> ObserverLease& = delete;
  auto operator->() const noexcept     -> ObserverTy*;
  auto operator*() const noexcept      -> ObserverTy&;

private:
  std::reference_wrapper<ObserverTy>                _observer;
  std::optional<std::reference_wrapper<LeaseCount>> _leases;
};
template <class ObserverTy>
ObserverLease<ObserverTy>::ObserverLease(ObserverTy& observer, LeaseCount& leases)
    : _observer{ observer }, _leases{ leases } {
  leases.Acquire();
}
template <class ObserverTy>
ObserverLease<ObserverTy>::ObserverLease(ObserverLease&& other) noexcept
    : _observer{ other._observer }, _leases{ std::exchange(other._leases, std::nullopt) } { }
template <class ObserverTy> ObserverLease<ObserverTy>::~ObserverLease() {
  if (_leases) _leases->get().Release();
}
template <class ObserverTy> auto ObserverLease<ObserverTy>::operator->() const noexcept -> ObserverTy* {
  return std::addressof(_observer.get());
}
template <class ObserverTy> auto ObserverLease<ObserverTy>::operator*() const noexcept -> ObserverTy& {
  return _observer.get();
}
}
