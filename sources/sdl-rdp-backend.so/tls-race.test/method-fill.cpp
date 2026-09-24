#include "tls-race.test/method-fill.hpp"

#include "_detail/contract.hpp"

#include <chrono>
#include <tuple>

namespace Race {
namespace {
constexpr auto StallLimit = std::chrono::seconds(2);
}
auto MethodFill::Shared() -> MethodFill& {
  static MethodFill fill;
  return fill;
}
auto MethodFill::Created(BIO_METHOD const* method, char const* name) -> void {
  utilities::Expects(method != nullptr, "OpenSSL allocated the method");
  utilities::Expects(name != nullptr, "the method is named");
  std::scoped_lock const lock(guard);
  created.insert_or_assign(name, method);
}
auto MethodFill::Filling(BIO_METHOD const* method, Setter setter) -> void {
  utilities::Expects(method != nullptr, "a method is being filled");
  std::unique_lock lock(guard);
  if (!Holds(method, setter)) return;
  ++fills;
  std::ignore = racer_done.wait_for(lock, StallLimit, [this] { return racers_done > 0; });
}
auto MethodFill::Arm(std::string_view method, Setter setter) -> void {
  std::scoped_lock const lock(guard);
  held_method = method;
  held_setter = setter;
  fills       = 0;
  racers_done = 0;
}
auto MethodFill::RacerDone() -> void {
  {
    std::scoped_lock const lock(guard);
    ++racers_done;
  }
  racer_done.notify_all();
}
auto MethodFill::Fills() -> unsigned {
  std::scoped_lock const lock(guard);
  return fills;
}
auto MethodFill::Seen(std::string_view method) -> bool {
  std::scoped_lock const lock(guard);
  return created.contains(method);
}
auto MethodFill::Named(BIO_METHOD const* method, std::string_view name) -> bool {
  std::scoped_lock const lock(guard);
  return Is(method, name);
}
auto MethodFill::Holds(BIO_METHOD const* method, Setter setter) const -> bool {
  return setter == held_setter && Is(method, held_method);
}
auto MethodFill::Is(BIO_METHOD const* method, std::string_view name) const -> bool {
  auto const found = created.find(name);
  return found != created.end() && found->second == method;
}
}
