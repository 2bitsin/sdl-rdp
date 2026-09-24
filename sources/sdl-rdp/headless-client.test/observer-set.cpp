#include <sdl-rdp/headless-client.test/observer-set.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace Headless {
namespace {
// freerdp.h ContextSize: FreeRDP allocates this many bytes and hands back the rdpContext at their start.
struct ObservedContext {
  rdpContext   context;
  ObserverSet* observers;
};
auto Observed(rdpContext& context) -> ObservedContext& {
  // C ABI: the rdpContext is the first member of the ObservedContext FreeRDP allocated.
  return *reinterpret_cast<ObservedContext*>(&context);
}
}
auto ObserverSet::ContextSize() -> std::size_t {
  return sizeof(ObservedContext);
}
auto ObserverSet::Of(rdpContext& context) -> ObserverSet& {
  auto* observers = Observed(context).observers;
  utilities::Expects(observers != nullptr, "the client context carries its observers");
  return *observers;
}
auto ObserverSet::Of(void* context) -> ObserverSet& {
  utilities::Expects(context != nullptr, "the channel event names its client context");
  return Of(*static_cast<rdpContext*>(context));
}
auto ObserverSet::Bind(rdpContext& context) -> void {
  Observed(context).observers = this;
}
auto ObserverSet::Insert(std::type_index kind, std::any const& observer) -> void {
  std::scoped_lock const lock(guard);
  utilities::Expects(!held.contains(kind), "one observer of a kind per client");
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
  utilities::Expects(!node.empty(), "the observer is attached to this client");
  return node;
}
auto ObserverSet::Attached(std::type_index kind) -> Registration& {
  auto const found = held.find(kind);
  utilities::Expects(found != held.end(), "the observer is attached to this client");
  return found->second;
}
}
