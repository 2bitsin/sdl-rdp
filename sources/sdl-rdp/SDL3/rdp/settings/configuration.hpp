#pragma once
#include "settings.hpp"
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <cstddef>
#include <cstdint>
namespace sdl3::rdp::settings::detail::configuration {
template <typename CredentialTy>
concept AuthenticationCredential = std::same_as<CredentialTy, char const*> || std::same_as<CredentialTy, std::uint8_t*>;
// SDL's display properties publish the application's C authentication callbacks with this signature.
template <AuthenticationCredential CredentialTy>
using AuthenticationCallback = bool(SDLCALL*)(void* user, char const* domain, char const* name, CredentialTy secret);
class Configuration {
public:
  // The backend calls these C callbacks with the opaque context.
       Configuration(Settings const& settings, decltype(sdlrdp_config::verify) verify,
                     decltype(sdlrdp_config::lookup) lookup, void* context);
       Configuration(Configuration const&)               = delete;
       Configuration(Configuration&&)                    = delete;
       ~Configuration()                                  = default;
  auto operator=(Configuration const&) -> Configuration& = delete;
  auto operator=(Configuration&&)      -> Configuration& = delete;
  auto Get() const                     -> sdlrdp_config const&;
private:
  static constexpr std::size_t                          _StringFields = 5;
  std::array<std::optional<std::string>, _StringFields> _strings      { };
  sdlrdp_config                                         _value        { };
};
}
namespace sdl3::rdp::settings {
using detail::configuration::AuthenticationCredential;
using detail::configuration::AuthenticationCallback;
using detail::configuration::Configuration;
}
