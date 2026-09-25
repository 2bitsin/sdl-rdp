#include <sdl-rdp/headless-client.test/backend/instance.hpp>

#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <string_view>
#include <utility>

namespace sdl_rdp::headless_client_test::backend::detail::instance {
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

auto BackendInstance::Open(Setup const& config, LogSink& log) -> void {
  auto const opened = TryOpen(config, log);
  ASSERT_TRUE(opened.has_value()) << opened.error();
}
auto BackendInstance::Open(Setup const& config, LogSink& log, CredentialCheck const& check) -> void {
  Opened(config, log, check);
}
auto BackendInstance::TryOpen(Setup const& config, LogSink& log) -> std::expected<void, std::string> {
  std::string failure;
  auto const  open    = [&] {
    _account = std::make_unique<Account>(config);
    Opened(config, log, *_account);
  };
  if (Contained(open, [&failure](std::string_view text) { failure = text; })) return { };
  Close();
  return std::unexpected(std::move(failure));
}
auto BackendInstance::Opened(Setup const& config, LogSink& log, CredentialCheck const& check) -> void {
  _backend = std::make_unique<Backend>(config, log, check);
}
auto BackendInstance::Close() noexcept -> void {
  _backend.reset();
  _account.reset();
}
auto BackendInstance::operator*() const -> Backend& {
  Expects(_backend != nullptr, "the backend is open");
  return *_backend;
}
BackendInstance::operator bool() const noexcept {
  return _backend != nullptr;
}
auto BackendInstance::Port() const -> std::uint32_t {
  return (**this).Port();
}
auto BackendInstance::Poll() const -> std::vector<Event> {
  return (**this).Events().Poll();
}
auto BackendInstance::Wait(std::chrono::milliseconds timeout) const -> bool {
  return (**this).Events().Wait(DeadlineAfter(timeout));
}
auto BackendInstance::WaitFrame(std::chrono::milliseconds timeout) const -> bool {
  return (**this).Presentation().WaitFrame(DeadlineAfter(timeout));
}
auto BackendInstance::WaitAudio(std::chrono::milliseconds timeout) const -> bool {
  return (**this).Audio().Wait(DeadlineAfter(timeout));
}
auto BackendInstance::Present(std::span<std::uint32_t const> pixels, FrameLayout const& layout,
                              std::span<Rect const> damage) const -> void {
  (**this).Presentation().Present(oxbox::utilities::SpanCast<std::uint8_t const>(pixels), layout, damage);
}
auto BackendInstance::Present(std::span<std::uint32_t const> pixels, std::uint32_t width, std::uint32_t height,
                              std::span<Rect const> damage) const -> void {
  Present(pixels, FrameLayout{ width, height, Narrowed<int>(width * sizeof(std::uint32_t)) }, damage);
}
auto BackendInstance::Present(std::span<std::uint32_t const> pixels, std::uint32_t width, std::uint32_t height,
                              Rect const& damage) const -> void {
  Present(pixels, width, height, std::span{ &damage, 1 });
}
}
