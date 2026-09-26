#pragma once
#include <sdl-rdp/auth/account.hpp>
#include <sdl-rdp/configuration/credential-check.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/picture/frame-layout.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sdl_rdp::headless_client_test::backend::detail::instance {
using sdl_rdp::auth::Account;
using sdl_rdp::configuration::CredentialCheck;
using sdl_rdp::configuration::Setup;
using sdl_rdp::diagnostics::LogSink;
using sdl_rdp::link::Event;
using sdl_rdp::picture::FrameLayout;
using sdl_rdp::session::Backend;
using sdl_rdp::utilities::Rect;

// A backend the test owns; without an explicit check the configured account answers authentication.
class BackendInstance {
public:
  auto     Open(Setup const& config, LogSink& log)                               -> void;
  auto     Open(Setup const& config, LogSink& log, CredentialCheck const& check) -> void;
  auto     TryOpen(Setup const& config, LogSink& log)                            -> std::expected<void, std::string>;
  auto     Close() noexcept                                                      -> void;
  auto     operator*() const                                                     -> Backend&;
  explicit operator bool() const noexcept;
  auto     Port() const                                                          -> std::uint32_t;
  auto     Poll() const                                                          -> std::vector<Event>;
  auto     Wait(std::chrono::milliseconds timeout) const                         -> bool;
  auto     WaitFrame(std::chrono::milliseconds timeout) const                    -> bool;
  auto     WaitAudio(std::chrono::milliseconds timeout) const                    -> bool;
  auto     Present(std::span<std::uint32_t const> pixels, FrameLayout const& layout, std::span<Rect const> damage) const
      -> void;
  auto     Present(std::span<std::uint32_t const> pixels, std::uint32_t width, std::uint32_t height,
                   std::span<Rect const> damage) const -> void;
  auto     Present(std::span<std::uint32_t const> pixels, std::uint32_t width, std::uint32_t height,
                   Rect const& damage) const -> void;

private:
  auto Opened(Setup const& config, LogSink& log, CredentialCheck const& check) -> void;
  std::unique_ptr<Account> _account;
  std::unique_ptr<Backend> _backend;
};
}

namespace sdl_rdp::headless_client_test::backend {
using detail::instance::BackendInstance;
}
