#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace sdl_rdp::utilities::detail::nt_owf {
// An NTLM one-way function output (MS-NLMP 3.3.1): a password equivalent, wiped wherever a copy ends.
class NtOwf {
public:
       NtOwf() noexcept                           = default;
       NtOwf(NtOwf const&) noexcept               = default;
       NtOwf(NtOwf&&) noexcept                    = default;
       ~NtOwf();
  auto operator=(NtOwf const&) noexcept -> NtOwf& = default;
  auto operator=(NtOwf&&) noexcept      -> NtOwf& = default;
  auto Bytes() noexcept                 -> std::span<std::uint8_t, 16>;
  auto Bytes() const noexcept           -> std::span<std::uint8_t const, 16>;

private:
  std::array<std::uint8_t, 16> _bytes{ };
};
}

namespace sdl_rdp::utilities {
using detail::nt_owf::NtOwf;
}
