#pragma once
#include <atomic>

namespace sdl_rdp::integration::tls_race_test::detail::injected_faults {
class InjectedFaults {
public:
  // The interposers are C functions without a user pointer, so their state is one object per process.
  static auto        Shared()                             -> InjectedFaults&;
  auto               RefusePrivateKey() noexcept          -> void;
  auto               SilenceClient() noexcept             -> void;
  auto               DropServerWrites() noexcept          -> void;
  [[nodiscard]] auto PrivateKeyRefused() const noexcept   -> bool;
  [[nodiscard]] auto ClientSilenced() const noexcept      -> bool;
  [[nodiscard]] auto ServerWritesDropped() const noexcept -> bool;

private:
  std::atomic_bool private_key_refused  { false };
  std::atomic_bool client_silenced      { false };
  std::atomic_bool server_writes_dropped{ false };
};
}

namespace sdl_rdp::integration::tls_race_test {
using detail::injected_faults::InjectedFaults;
}
