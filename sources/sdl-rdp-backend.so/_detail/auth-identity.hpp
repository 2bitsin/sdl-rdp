#pragma once
#include "contract.hpp"
#include "transcode.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <winpr/sspi.h>

namespace Backend {
inline std::string QualifiedName(std::string_view domain, std::string_view user) {
  return domain.empty() ? std::string(user) : std::string(domain) + "\\" + std::string(user);
}
inline std::string IdentityText(UINT16 const* text, ULONG length, ULONG flags) {
  if (length) utilities::Expects(text != nullptr, "identity buffer covers length");
  using oxbox::utilities::Encoding;
  // FreeRDP on Linux fills ANSI identities from UTF-8 settings (3.15 winpr/libwinpr/sspi/sspi_winpr.c).
  auto unicode = (flags & SEC_WINNT_AUTH_IDENTITY_UNICODE) != 0;
  auto bytes   =
      std::span(reinterpret_cast<std::byte const*>(text), static_cast<std::size_t>(length) * (unicode ? 2 : 1));
  return TranscodeRange<std::string>(
      bytes, { .encoding = unicode ? Encoding::UTF16 : Encoding::UTF8, .order = std::endian::native }, { });
}
}
