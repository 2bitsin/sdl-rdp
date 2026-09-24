#pragma once
#include <openssl/bio.h>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace Race {
enum class Setter{ Write, Create };
// FreeRDP 3.32's lazily filled methods: tcp.c:431 socket method and tls.c:672 TLS method (2bitsin/FreeRDP#2).
inline constexpr std::string_view SocketMethod = "SimpleSocket";
inline constexpr std::string_view TlsMethod    = "RdpTls";

// Holds a lazily filled BIO method half-filled at one setter, as a slow first fill would, until a rival is done.
class MethodFill {
public:
  // The interposers are C functions without a user pointer, so their state is one object per process.
  static auto        Shared()                                               -> MethodFill&;
  auto               Created(BIO_METHOD const* method, char const* name)    -> void;
  auto               Filling(BIO_METHOD const* method, Setter setter)       -> void;
  auto               Arm(std::string_view method, Setter setter)            -> void;
  auto               RacerDone()                                            -> void;
  [[nodiscard]] auto Fills()                                                -> std::size_t;
  [[nodiscard]] auto Seen(std::string_view method)                          -> bool;
  [[nodiscard]] auto Named(BIO_METHOD const* method, std::string_view name) -> bool;

private:
  [[nodiscard]] auto Holds(BIO_METHOD const* method, Setter setter) const      -> bool;
  [[nodiscard]] auto Is(BIO_METHOD const* method, std::string_view name) const -> bool;
  std::mutex                                            guard;
  std::condition_variable                               racer_done;
  std::map<std::string, BIO_METHOD const*, std::less<>> created;
  std::string                                           held_method;
  Setter                                                held_setter = Setter::Write;
  std::size_t                                           fills       = 0;
  std::size_t                                           racers_done = 0;
};
}
