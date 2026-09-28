#include <sdl-rdp/utilities/socket-library.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <system_error>

namespace sdl_rdp::utilities::detail::socket_library {
SocketLibrary::SocketLibrary() {
  if (auto const failed = Start()) throw std::system_error(failed, std::system_category(), "Socket library startup");
}
SocketLibrary::SocketLibrary([[maybe_unused]] SocketLibrary const& other) noexcept {
  Share();
}
SocketLibrary::SocketLibrary([[maybe_unused]] SocketLibrary&& other) noexcept {
  Share();
}
SocketLibrary::~SocketLibrary() {
  Stop();
}
auto SocketLibrary::Share() noexcept -> void {
  auto const failed = Start();
  Ensures(failed == 0, "the library takes another reference while one is held");
}
}
