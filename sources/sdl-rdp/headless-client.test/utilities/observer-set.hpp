#pragma once
#include "observer-lease.hpp"
#include <sdl-rdp/headless-client.test/backend/lease-count.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/scoped.hpp>

#include <freerdp/freerdp.h>
#include <any>
#include <cstddef>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace sdl_rdp::headless_client_test::utilities::detail::observer_set {
using sdl_rdp::headless_client_test::backend::LeaseCount;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::RAIIWrap;

class ObserverSet {
public:
  static auto                      ContextSize()             -> std::size_t;
  static auto                      Of(rdpContext& context)   -> ObserverSet&;
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
template <class ObserverTy> auto Join(rdpContext& context, ObserverTy& observer) -> rdpContext& {
  ObserverSet::Of(context).Add(observer);
  return context;
}
template <class ObserverTy> auto Leave(rdpContext& context) noexcept -> void {
  ObserverSet::Of(context).Remove<ObserverTy>();
}
// The client's callbacks reach the observer for the holder's lifetime; the release waits out their leases.
template <class ObserverTy> using Membership = RAIIWrap<rdpContext&, Join<ObserverTy>, Leave<ObserverTy>>;
template <class OwnerTy, class MemberTy> auto OwnerOf(MemberTy OwnerTy::*) -> OwnerTy;
// abi: pDesktopResize and pEndPaint, BOOL is int; the context's observer answers through MEMBER.
template <auto MEMBER> auto Delegated(rdpContext* context) -> int {
  Expects(context != nullptr, "the callback names its client context");
  return std::invoke(MEMBER, *ObserverSet::Of(*context).Held<decltype(OwnerOf(MEMBER))>(), *context);
}
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::observer_set::Membership;
using detail::observer_set::Delegated;
using detail::observer_set::ObserverSet;
using detail::observer_set::OwnerOf;
}
