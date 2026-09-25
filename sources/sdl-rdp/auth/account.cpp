#include <sdl-rdp/auth/account.hpp>

#include <sdl-rdp/freerdp-facade/ntlm.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <openssl/crypto.h>

namespace sdl_rdp::auth::detail::account {
using sdl_rdp::freerdp_facade::NtOwfV1;
using sdl_rdp::utilities::Utf16;

Account::Account(Setup const& setup) : _user{ setup.user }, _password{ setup.password }, _domain{ setup.domain } { }
auto Account::Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool {
  auto const expected = Password(domain, user);
  if (!expected) return false;
  return expected->size() == password.size() && CRYPTO_memcmp(expected->data(), password.data(), password.size()) == 0;
}
auto Account::NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> {
  auto const expected = Password(domain, user);
  if (!expected) return std::nullopt;
  return NtOwfV1(Utf16(*expected));
}
auto Account::Password(std::string_view domain, std::string_view user) const noexcept
    -> std::optional<std::string_view> {
  if (!_user) return std::nullopt;
  if (!_password) return std::nullopt;
  if (*_user != user) return std::nullopt;
  if (_domain && *_domain != domain) return std::nullopt;
  return _password->Text();
}
}
