#pragma once
#include <sdl-rdp/utilities/nt-owf.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <optional>
#include <string_view>

namespace sdl_rdp::configuration::detail::credential_check {
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::Pinned;

// Answers a peer's logon: a TLS password pair, or the NT hash NLA needs for a user.
class CredentialCheck : private Pinned {
public:
  virtual      ~CredentialCheck()                                                                   = default;
  virtual auto Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool = 0;
  virtual auto NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> = 0;
};
}

namespace sdl_rdp::configuration {
using detail::credential_check::CredentialCheck;
}
