#pragma once

namespace sdl_rdp::utilities::detail::socket_library {
// A counted reference on the platform's socket library (Winsock on Windows); assignment keeps both references.
class SocketLibrary {
public:
  SocketLibrary();
  // A move is a copy: the source keeps its own reference.
       SocketLibrary(SocketLibrary const& other)                        noexcept;
       SocketLibrary(SocketLibrary&& other)                             noexcept;
       ~SocketLibrary();
  auto operator=(SocketLibrary const& other) noexcept -> SocketLibrary& = default;
  auto operator=(SocketLibrary&& other) noexcept      -> SocketLibrary& = default;

private:
  // The source holds a reference, so another cannot fail.
  static auto               Share() noexcept -> void;
  [[nodiscard]] static auto Start() noexcept -> int;
  static auto               Stop() noexcept  -> void;
};
}

namespace sdl_rdp::utilities {
using detail::socket_library::SocketLibrary;
}
