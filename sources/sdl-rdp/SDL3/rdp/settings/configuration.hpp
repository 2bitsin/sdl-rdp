#pragma once
#include "options.hpp"
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <sdl-rdp/settings/aspect.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
namespace sdl3::rdp::settings::detail::configuration {
template <typename CredentialTy>
concept AuthenticationCredential = std::same_as<CredentialTy, char const*> || std::same_as<CredentialTy, std::uint8_t*>;
// SDL's display properties publish the application's C authentication callbacks with this signature.
template <AuthenticationCredential CredentialTy>
using AuthenticationCallback = bool(SDLCALL*)(void* user, char const* domain, char const* name, CredentialTy secret);
// A backend configuration C string and the setting text it points into.
using ConfigurationString  = std::pair<char const * sdlrdp_config::*, std::optional<std::string>>;
using ConfigurationStrings = std::array<ConfigurationString, 5>;
// The backend reads a zero ratio as square pixels, which is what no stated aspect means.
auto BackendAspect(sdl_rdp::settings::Aspect const& aspect) -> sdlrdp_aspect;
class Configuration {
public:
  // The backend calls these C callbacks with the opaque context.
  Configuration(Options const& options, decltype(sdlrdp_config::verify) verify, decltype(sdlrdp_config::lookup) lookup,
                void* context);
       Configuration(Configuration const&)               = delete;
       Configuration(Configuration&&)                    = delete;
       ~Configuration()                                  = default;
  auto operator=(Configuration const&) -> Configuration& = delete;
  auto operator=(Configuration&&)      -> Configuration& = delete;
  auto Get() const                     -> sdlrdp_config const&;
private:
  ConfigurationStrings _strings;
  sdlrdp_config        _value  { };
};
}
namespace sdl3::rdp::settings {
using detail::configuration::AuthenticationCredential;
using detail::configuration::AuthenticationCallback;
using detail::configuration::BackendAspect;
using detail::configuration::Configuration;
}
