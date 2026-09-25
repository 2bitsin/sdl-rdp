#include <sdl-rdp/freerdp-facade/ntlm.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>

#include <oxbox/utilities/span.hpp>
#include <winpr/ntlm.h>
#include <string>

namespace sdl_rdp::freerdp_facade::detail::ntlm {
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Wipe;

namespace {
// WinPR 3.32 ntlm.h:41 takes every string as a writable, terminated LPWSTR measured in bytes.
class WideArgument {
public:
  explicit WideArgument(std::u16string_view text) : _text{ text } { }
           WideArgument(WideArgument const&) = delete;
           WideArgument(WideArgument&&)      = delete;
           ~WideArgument() {
    Wipe(std::as_writable_bytes(std::span{ _text }));
  }
  auto operator=(WideArgument const&) -> WideArgument& = delete;
  auto operator=(WideArgument&&)      -> WideArgument& = delete;
  auto Units()                        -> std::span<std::uint16_t> {
    return oxbox::utilities::SpanCast<std::uint16_t>(std::span(_text));
  }
  auto Bytes() const -> std::uint32_t {
    return Narrowed<std::uint32_t>(_text.size() * sizeof(char16_t));
  }

private:
  std::u16string _text;
};
}
auto NtOwfV1(std::u16string_view password) -> NtOwf {
  WideArgument written{ password };
  NtOwf        hash;
  if (!NTOWFv1W(written.Units().data(), written.Bytes(), hash.Bytes().data())) throw NtlmHashFailed{ "v1" };
  return hash;
}
auto NtOwfV2(NtOwf const& v1, std::u16string_view user, std::u16string_view domain) -> NtOwf {
  NtOwf        key    { v1     };
  WideArgument account{ user   };
  WideArgument realm  { domain };
  NtOwf        hash;
  if (!NTOWFv2FromHashW(key.Bytes().data(), account.Units().data(), account.Bytes(), realm.Units().data(),
                        realm.Bytes(), hash.Bytes().data()))
    throw NtlmHashFailed{ "v2" };
  return hash;
}
}
