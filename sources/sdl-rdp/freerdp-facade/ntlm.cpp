#include <sdl-rdp/freerdp-facade/ntlm.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/parameter.hpp>

#include <oxbox/utilities/span.hpp>
#include <winpr/ntlm.h>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace sdl_rdp::freerdp_facade::detail::ntlm {
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Parameter;

namespace {
// abi: WinPR's WCHAR is wchar_t under Windows and a 16-bit integer elsewhere, a UTF-16 unit on both.
using WideUnit = std::remove_pointer_t<Parameter<NTOWFv1W, 0>>;
static_assert(sizeof(WideUnit) == sizeof(char16_t));

// abi: WinPR refuses a null password or user even at length 0, a null domain only past it (utils/ntlm.c:36,161,164).
auto Units(std::u16string_view text) -> std::span<WideUnit> {
  return oxbox::utilities::SpanCast<WideUnit>(std::span{ text.empty() ? std::u16string_view{ u"" } : text });
}
auto Bytes(std::u16string_view text) -> std::uint32_t {
  return Narrowed<std::uint32_t>(std::span{ text }.size_bytes());
}
}
auto NtOwfV1(std::u16string_view password) -> NtOwf {
  NtOwf hash;
  if (!NTOWFv1W(Units(password).data(), Bytes(password), hash.Bytes().data())) throw NtlmHashFailed{ "v1" };
  return hash;
}
auto NtOwfV2(NtOwf const& v1, std::u16string_view user, std::u16string_view domain) -> NtOwf {
  NtOwf key { v1 };
  NtOwf hash;
  if (!NTOWFv2FromHashW(key.Bytes().data(), Units(user).data(), Bytes(user), Units(domain).data(), Bytes(domain),
                        hash.Bytes().data()))
    throw NtlmHashFailed{ "v2" };
  return hash;
}
}
