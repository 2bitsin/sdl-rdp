#include "injected-faults.hpp"

namespace sdl_rdp::integration::tls_race_test::detail::injected_faults {
auto InjectedFaults::Shared() -> InjectedFaults& {
  static InjectedFaults faults;
  return faults;
}
auto InjectedFaults::RefusePrivateKey() noexcept -> void {
  private_key_refused = true;
}
auto InjectedFaults::SilenceClient() noexcept -> void {
  client_silenced = true;
}
auto InjectedFaults::DropServerWrites() noexcept -> void {
  server_writes_dropped = true;
}
auto InjectedFaults::PrivateKeyRefused() const noexcept -> bool {
  return private_key_refused;
}
auto InjectedFaults::ClientSilenced() const noexcept -> bool {
  return client_silenced;
}
auto InjectedFaults::ServerWritesDropped() const noexcept -> bool {
  return server_writes_dropped;
}
}
