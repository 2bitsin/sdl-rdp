#include "credential-relay.hpp"
#include <oxbox/utilities/fixed-string.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
namespace sdl3::rdp::detail::credential_relay {
using oxbox::utilities::FixedString;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::WipedString;
namespace {
// The display properties publish the application's C callbacks: user data, domain, name, then the password or the hash.
template <typename LastTy> using Callback = bool(SDLCALL*)(void*, char const*, char const*, LastTy*);
// Asks the callback at the named property, read at call time; no display or no callback is "not published".
// The name is the property macro's literal held with its terminator; the last argument spans a std::string or the
// hash bytes.
template <FixedString NAME, typename LastTy>
auto Ask(SDL_PropertiesID properties, std::string_view domain, std::string_view user, std::span<LastTy> last)
    -> std::optional<bool> {
  if (properties == 0) return std::nullopt;
  auto const callback = reinterpret_cast<Callback<LastTy>>(SDL_GetPointerProperty(properties, NAME.data, nullptr));
  if (callback == nullptr) return std::nullopt;
  auto* const data = SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, nullptr);
  return callback(data, std::string{ domain }.c_str(), std::string{ user }.c_str(), last.data());
}
}
CredentialRelay::CredentialRelay(Setup const& setup) : _account{ setup } { }
auto CredentialRelay::Verifies(std::string_view domain, std::string_view user, std::string_view password) const
    -> bool {
  WipedString const text  { password };
  auto const        asked = Ask<SDL_PROP_DISPLAY_RDP_VERIFY_POINTER>(_display.load(), domain, user,
                                                                     std::span<char const>{ text.Text() });
  return asked ? *asked : _account.Verifies(domain, user, password);
}
auto CredentialRelay::NtHash(std::string_view domain, std::string_view user) const -> std::optional<NtOwf> {
  NtOwf      hash;
  auto const asked = Ask<SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER>(_display.load(), domain, user,
                                                              std::span<std::uint8_t>{ hash.Bytes() });
  if (!asked) return _account.NtHash(domain, user);
  if (!*asked) return std::nullopt;
  return hash;
}
auto CredentialRelay::Display(SDL_PropertiesID properties) noexcept -> void {
  _display.store(properties);
}
auto PublishAuthentication(CredentialRelay& relay, SDL_PropertiesID properties) -> DisplayAuthentication {
  Expects(properties != 0, "authentication reports to a display's properties");
  relay.Display(properties);
  return { relay, properties };
}
auto WithdrawAuthentication(DisplayAuthentication const& authentication) noexcept -> void {
  authentication.first.get().Display(0);
}
}
