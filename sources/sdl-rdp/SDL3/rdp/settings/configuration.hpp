#pragma once
#include "configured-text.hpp"
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
using sdl_rdp::settings::Aspect;

template <typename CredentialTy>
concept AuthenticationCredential = std::same_as<CredentialTy, char const*> || std::same_as<CredentialTy, std::uint8_t*>;
// SDL's display properties publish the application's C authentication callbacks with this signature.
template <AuthenticationCredential CredentialTy>
using AuthenticationCallback = bool(SDLCALL*)(void* user, char const* domain, char const* name, CredentialTy secret);
// The backend reads a zero ratio as square pixels, which is what no stated aspect means.
auto BackendAspect(Aspect const& aspect) -> sdlrdp_aspect;
// The settings the backend opens with, read once; the ABI record is built over the text held here.
class Configuration {
public:
  explicit Configuration(Options const& options);
  auto     Get() const          -> sdlrdp_config;
  auto     Width() const        -> std::uint32_t;
  auto     Height() const       -> std::uint32_t;
  auto     AudioLatency() const -> std::uint32_t;
private:
  ConfiguredText _text;
  std::uint32_t  _port;
  std::uint32_t  _width;
  std::uint32_t  _height;
  std::uint32_t  _audio_latency_ms;
  std::uint32_t  _avc_bitrate_kbps;
  bool           _wait_for_client;
  sdlrdp_codec   _codec;
  sdlrdp_aspect  _aspect;
  sdlrdp_auth    _auth;
};
}

namespace sdl3::rdp::settings {
using detail::configuration::AuthenticationCallback;
using detail::configuration::AuthenticationCredential;
using detail::configuration::BackendAspect;
using detail::configuration::Configuration;
}
