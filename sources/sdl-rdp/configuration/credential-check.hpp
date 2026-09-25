#pragma once
#include <sdl-rdp/utilities/nt-owf.hpp>

#include <optional>
#include <string_view>

namespace sdl_rdp::configuration::detail::credential_check {
using sdl_rdp::utilities::NtOwf;

// Answers a peer's logon: a TLS password pair, or the NT hash NLA needs for a user.
class CredentialCheck {
public:
               CredentialCheck()                                                                    = default;
               CredentialCheck(CredentialCheck const&)                                              = delete;
               CredentialCheck(CredentialCheck&&)                                                   = delete;
  virtual      ~CredentialCheck();
  auto         operator=(CredentialCheck const&)                            -> CredentialCheck&     = delete;
  auto         operator=(CredentialCheck&&)                                 -> CredentialCheck&     = delete;
  virtual auto Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool = 0;
  virtual auto NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> = 0;
};
}

namespace sdl_rdp::configuration {
using detail::credential_check::CredentialCheck;
}
