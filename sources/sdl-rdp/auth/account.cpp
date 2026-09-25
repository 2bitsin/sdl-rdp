#include <sdl-rdp/auth/account.hpp>

#include <sdl-rdp/utilities/transcode.hpp>

#include <openssl/crypto.h>

namespace sdl_rdp::auth::detail::account {
using sdl_rdp::freerdp_facade::NtOwfV1;
using sdl_rdp::utilities::Utf16;

Account::Account(sdlrdp_config const& config) noexcept : _config{ config } { }
auto Account::Verifies(std::string_view domain, std::string_view user, std::string_view password) const noexcept
    -> bool {
  if (!PairName(domain, user)) return false;
  std::string_view const expected{ _config.password };
  return expected.size() == password.size() && CRYPTO_memcmp(expected.data(), password.data(), password.size()) == 0;
}
auto Account::NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> {
  if (!PairName(domain, user)) return std::nullopt;
  return NtOwfV1(Utf16(_config.password));
}
auto Account::PairName(std::string_view domain, std::string_view user) const noexcept -> bool {
  if (!_config.user) return false;
  if (!_config.password) return false;
  return _config.user == user && (!_config.domain || _config.domain == domain);
}
}
