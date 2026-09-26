#pragma once
#include <sdl-rdp/configuration/credential-check.hpp>
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace sdl_rdp::configuration::detail::configuration {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::CredentialCheck;
using sdl_rdp::configuration::Refresh;
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::configuration::Setup;
using sdl_rdp::utilities::Pinned;

class Configuration : private Pinned {
public:
  explicit Configuration(Setup const& setup, CredentialCheck const& credentials);
  auto     Config() const noexcept                             -> Setup const&;
  auto     Credentials() const noexcept                        -> CredentialCheck const&;
  auto     CertificateDirectory() const noexcept               -> std::filesystem::path const&;
  auto     Auth() const noexcept                               -> AuthMode;
  auto     CodecPreference() const noexcept                    -> Codec;
  auto     SetCodec(Codec value)                               -> void;
  auto     AvcBitrate() const noexcept                         -> std::uint32_t;
  auto     AudioLatency() const noexcept                       -> std::uint32_t;
  auto     RefreshPolicy() const noexcept                      -> Refresh const&;
  auto     SetRefresh(RefreshMode mode, std::uint32_t ceiling) -> void;

private:
  Setup                  _setup;
  CredentialCheck const& _credentials;
  std::filesystem::path  _certificate_directory;
  std::atomic<Codec>     _codec;
  std::uint32_t          _audio_latency;
  Refresh                _refresh;
};
}

namespace sdl_rdp::configuration {
using detail::configuration::Configuration;
}
