#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/freerdp-facade/ntlm.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <optional>
#include <string_view>

namespace sdl_rdp::auth::detail::account {
using sdl_rdp::freerdp_facade::NtOwf;
using sdl_rdp::utilities::Pinned;

// The fixed user, domain and password sdlrdp_config names: the check used when the app sets no callback.
class Account : private Pinned {
public:
  explicit Account(sdlrdp_config const& config) noexcept;
  auto Verifies(std::string_view domain, std::string_view user, std::string_view password) const noexcept -> bool;
  auto     NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf>;

private:
  auto PairName(std::string_view domain, std::string_view user) const noexcept -> bool;
  sdlrdp_config const& _config;
};
}

namespace sdl_rdp::auth {
using detail::account::Account;
}
