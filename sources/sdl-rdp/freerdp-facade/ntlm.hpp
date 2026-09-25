#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::ntlm {
// An NTLM one-way function output (MS-NLMP 3.3.1): a password equivalent, scrubbed wherever a copy ends.
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
auto NtOwfV1(std::u16string_view password)                                          -> NtOwf;
auto NtOwfV2(NtOwf const& v1, std::u16string_view user, std::u16string_view domain) -> NtOwf;
}
namespace sdl_rdp::freerdp_facade {
using detail::ntlm::NtOwf;
using detail::ntlm::NtOwfV1;
using detail::ntlm::NtOwfV2;
}
