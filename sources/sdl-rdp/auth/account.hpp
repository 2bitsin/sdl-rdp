#pragma once
#include <sdl-rdp/configuration/credential-check.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/utilities/nt-owf.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace sdl_rdp::auth::detail::account {
using sdl_rdp::configuration::CredentialCheck;
using sdl_rdp::configuration::Setup;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::WipedString;

// The fixed user, domain and password Setup names: the check used when the app supplies none.
class Account final : public CredentialCheck {
public:
  explicit Account(Setup const& setup);
  auto Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool override;
  auto     NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> override;

private:
  auto Password(std::string_view domain, std::string_view user) const noexcept -> std::optional<std::string_view>;
  std::optional<std::string> _user;
  std::optional<WipedString> _password;
  std::optional<std::string> _domain;
};
}

namespace sdl_rdp::auth {
using detail::account::Account;
}
