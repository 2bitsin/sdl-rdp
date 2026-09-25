#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <oxbox/utilities/span.hpp>
#include <winpr/sspi.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace sdl_rdp::auth::detail::identity {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::TranscodeRange;

struct IdentityNames {
  std::string user;
  std::string domain;
};
inline auto QualifiedName(std::string_view domain, std::string_view user) -> std::string {
  return domain.empty() ? std::string(user) : std::string(domain) + "\\" + std::string(user);
}
inline auto IdentityText(std::u16string_view utf16) -> std::string {
  using oxbox::utilities::Encoding;
  return TranscodeRange<std::string>(oxbox::utilities::AsBytes(utf16),
                                     { .encoding = Encoding::UTF16, .order = std::endian::native }, { });
}
inline auto IdentityText(std::string_view utf8) -> std::string {
  using oxbox::utilities::Encoding;
  return TranscodeRange<std::string>(oxbox::utilities::AsBytes(utf8),
                                     { .encoding = Encoding::UTF8, .order = std::endian::native }, { });
}
// WinPR types identity text as BYTE for UTF-8 and UINT16 for UTF-16; the code units are the text.
inline auto IdentityView(std::span<std::uint8_t const> utf8) -> std::string_view {
  return std::string_view{ oxbox::utilities::SpanCast<char const>(utf8) };
}
inline auto IdentityView(std::span<std::uint16_t const> utf16) -> std::u16string_view {
  return std::u16string_view{ oxbox::utilities::SpanCast<char16_t const>(utf16) };
}
template <auto TEXT, auto LENGTH, class Identity> auto IdentityField(Identity const& identity) -> decltype(auto) {
  auto const* const text   = identity.*TEXT;
  auto const        length = std::size_t{ identity.*LENGTH };
  if (length) Expects(text != nullptr, "identity buffer covers length");
  return IdentityView(std::span{ text, length });
}
template <class Identity> auto NamesOf(Identity const& identity) -> IdentityNames {
  return { .user   = IdentityText(IdentityField<&Identity::User, &Identity::UserLength>(identity)),
           .domain = IdentityText(IdentityField<&Identity::Domain, &Identity::DomainLength>(identity)) };
}
inline auto ClientNames(SEC_WINNT_AUTH_IDENTITY const& identity) -> IdentityNames {
  if ((identity.Flags & SEC_WINNT_AUTH_IDENTITY_UNICODE) != 0) return NamesOf(identity);
  // FreeRDP on Linux fills ANSI identities from UTF-8 settings (3.32 winpr/libwinpr/sspi/sspi_winpr.c:621).
  // WinPR declares the W and A identities with one layout (sspi.h); Flags says which pointer type is live.
  return NamesOf(std::bit_cast<SEC_WINNT_AUTH_IDENTITY_A>(identity));
}
}

namespace sdl_rdp::auth {
using detail::identity::ClientNames;
using detail::identity::QualifiedName;
}
