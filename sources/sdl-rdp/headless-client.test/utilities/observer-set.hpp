#pragma once
#include "observer-lease.hpp"
#include <sdl-rdp/headless-client.test/backend/lease-count.hpp>

#include <freerdp/freerdp.h>
#include <any>
#include <cstddef>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace sdl_rdp::headless_client_test::utilities::detail::observer_set {
using sdl_rdp::headless_client_test::backend::LeaseCount;

class ObserverSet {
public:
  static auto                      ContextSize()             -> std::size_t;
  static auto                      Of(rdpContext& context)   -> ObserverSet&;
  static auto                      Of(void* context)         -> ObserverSet&;
  auto                             Bind(rdpContext& context) -> void;
  template <class ObserverTy> auto Add(ObserverTy& observer) -> void;
  template <class ObserverTy> auto Remove()                  -> void;
  template <class ObserverTy> auto Held()                    -> ObserverLease<ObserverTy>;

private:
  struct Registration {
    std::any   observer;
    LeaseCount leases;
  };
  using Registrations = std::unordered_map<std::type_index, Registration>;
  auto Insert(std::type_index kind, std::any const& observer) -> void;
  auto Withdraw(std::type_index kind)                         -> void;
  auto Detach(std::type_index kind)                           -> Registrations::node_type;
  auto Attached(std::type_index kind)                         -> Registration&;
  std::mutex    guard;
  Registrations held;
};
template <class ObserverTy> auto ObserverSet::Add(ObserverTy& observer) -> void {
  Insert(typeid(ObserverTy), std::ref(observer));
}
template <class ObserverTy> auto ObserverSet::Remove() -> void {
  Withdraw(typeid(ObserverTy));
}
// The lease is taken under the guard, so Remove either sees it outstanding or the lookup fails.
template <class ObserverTy> auto ObserverSet::Held() -> ObserverLease<ObserverTy> {
  std::scoped_lock const lock(guard);
  auto&                  found = Attached(typeid(ObserverTy));
  return { std::any_cast<std::reference_wrapper<ObserverTy>>(found.observer).get(), found.leases };
}
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::observer_set::ObserverSet;
}
