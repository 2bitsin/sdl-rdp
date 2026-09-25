#pragma once
#include <sdl-rdp/SDL3/rdp/sdl/internals.hpp>
#include <sdl-rdp/auth/account.hpp>
#include <sdl-rdp/configuration/credential-check.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/utilities/nt-owf.hpp>
#include <sdl-rdp/utilities/scoped.hpp>
#include <atomic>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
namespace sdl3::rdp::detail::credential_relay {
using sdl_rdp::auth::Account;
using sdl_rdp::configuration::CredentialCheck;
using sdl_rdp::configuration::Setup;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::RAIIWrap;

// A logon asks the application's callbacks on the display's properties, read at call time; the hint pair otherwise.
class CredentialRelay final : public CredentialCheck {
public:
  explicit CredentialRelay(Setup const& setup);
  auto Verifies(std::string_view domain, std::string_view user, std::string_view password) const -> bool override;
  auto     NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> override;
  auto     Display(SDL_PropertiesID properties) noexcept                -> void;
private:
  Account                       _account;
  std::atomic<SDL_PropertiesID> _display;
};
// The relay reports to this display's properties until it is withdrawn.
using DisplayAuthentication = std::pair<std::reference_wrapper<CredentialRelay>, SDL_PropertiesID>;
auto PublishAuthentication(CredentialRelay& relay, SDL_PropertiesID properties)   -> DisplayAuthentication;
auto WithdrawAuthentication(DisplayAuthentication const& authentication) noexcept -> void;
using AuthenticationDisplay = RAIIWrap<DisplayAuthentication, PublishAuthentication, WithdrawAuthentication>;
}

namespace sdl3::rdp {
using detail::credential_relay::AuthenticationDisplay;
using detail::credential_relay::CredentialRelay;
}
