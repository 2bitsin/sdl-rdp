#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>

namespace sdl_rdp::headless_client_test::utilities::detail::observer_set {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Required;

namespace {
// freerdp.h ContextSize: FreeRDP allocates this many zeroed bytes and hands back the rdpContext at their start.
struct ObservedContext {
  rdpContext                                         context  { };
  std::optional<std::reference_wrapper<ObserverSet>> observers;
};
static_assert(std::is_standard_layout_v<ObservedContext>);
static_assert(offsetof(ObservedContext, context) == 0);
// libstdc++: all-zero bytes are a disengaged optional<reference_wrapper>, so Of before Bind fails its contract.
auto Observed(rdpContext& context) -> ObservedContext& {
  // C ABI: the rdpContext is the first member of the ObservedContext FreeRDP allocated.
  return *reinterpret_cast<ObservedContext*>(&context);
}
}
auto ObserverSet::ContextSize() -> std::size_t {
  return sizeof(ObservedContext);
}
auto ObserverSet::Of(rdpContext& context) -> ObserverSet& {
  return Required(Observed(context).observers, "the client context carries its observers").get();
}
auto ObserverSet::Bind(rdpContext& context) -> void {
  Observed(context).observers = *this;
}
auto ObserverSet::Insert(std::type_index kind, std::any const& observer) -> void {
  std::scoped_lock const lock(guard);
  Expects(!held.contains(kind), "one observer of a kind per client");
  held.try_emplace(kind, observer);
}
// The wait runs outside the guard: a callback holding a lease may still need the set to look up another observer.
auto ObserverSet::Withdraw(std::type_index kind) -> void {
  auto const detached = Detach(kind);
  detached.mapped().leases.Drain();
}
auto ObserverSet::Detach(std::type_index kind) -> Registrations::node_type {
  std::scoped_lock const lock(guard);
  auto                   node = held.extract(kind);
  Expects(!node.empty(), "the observer is attached to this client");
  return node;
}
auto ObserverSet::Attached(std::type_index kind) -> Registration& {
  auto const found = held.find(kind);
  Expects(found != held.end(), "the observer is attached to this client");
  return found->second;
}
}
