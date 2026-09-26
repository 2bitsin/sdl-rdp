#pragma once
#include "credential-relay.hpp"
#include "log-relay.hpp"
#include <sdl-rdp/SDL3/rdp/settings/options.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
namespace sdl3::rdp::detail::driver {
using sdl_rdp::utilities::Pinned;

// The process's one RDP session: the settings read once, the backend built over them, the relays it reports through.
class Driver : private Pinned {
public:
       Driver();
  auto Options() const noexcept -> sdl3::rdp::settings::Options const&;
  auto Config() const noexcept  -> sdl_rdp::configuration::Setup const&;
  auto Credentials() noexcept   -> CredentialRelay&;
  auto Backend() noexcept       -> sdl_rdp::session::Backend&;
private:
  sdl3::rdp::settings::Options const  _options;
  sdl_rdp::configuration::Setup const _config;
  LogRelay                            _log;
  CredentialRelay                     _credentials;
  sdl_rdp::session::Backend           _backend;
};
}

namespace sdl3::rdp {
using detail::driver::Driver;
}
