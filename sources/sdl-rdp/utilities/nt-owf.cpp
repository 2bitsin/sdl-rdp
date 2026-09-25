#include <sdl-rdp/utilities/nt-owf.hpp>

#include <sdl-rdp/utilities/wiped-string.hpp>

namespace sdl_rdp::utilities::detail::nt_owf {
NtOwf::~NtOwf() {
  Wipe(std::as_writable_bytes(std::span{ _bytes }));
}
auto NtOwf::Bytes() noexcept -> std::span<std::uint8_t, 16> {
  return _bytes;
}
auto NtOwf::Bytes() const noexcept -> std::span<std::uint8_t const, 16> {
  return _bytes;
}
}
