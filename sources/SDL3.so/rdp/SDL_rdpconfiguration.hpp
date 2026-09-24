#pragma once
#include "SDL_rdpboundary.hpp"
#include "SDL_rdpsettings.hpp"
namespace rdp {
template <typename _Credential>
concept AuthenticationCredential = std::same_as<_Credential, char const*> || std::same_as<_Credential, unsigned char*>;
// SDL's display properties publish the application's C authentication callbacks with this signature.
template <AuthenticationCredential _Credential>
using AuthenticationCallback = bool(SDLCALL*)(void* user, char const* domain, char const* name, _Credential secret);
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
