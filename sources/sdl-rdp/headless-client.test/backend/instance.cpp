#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/contract.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstdint>

namespace sdl_rdp::headless_client_test::backend::detail::instance {
using sdl_rdp::utilities::Expects;

auto BackendInstance::Open(sdlrdp_config const& config) -> void {
  ASSERT_EQ(TryOpen(config), 0) << sdlrdp_last_error();
}
auto BackendInstance::TryOpen(sdlrdp_config const& config) -> int {
  sdlrdp_handle* opened = nullptr;
  auto const     result = sdlrdp_open(&config, &opened);
  if (result == 0) _handle.reset(opened);
  return result;
}
auto BackendInstance::Close() noexcept -> void {
  _handle.reset();
}
auto BackendInstance::Handle() const noexcept -> sdlrdp_handle* {
  return _handle.get();
}
auto BackendInstance::operator*() const -> sdlrdp_handle& {
  Expects(_handle != nullptr, "the backend is open");
  return *_handle;
}
BackendInstance::operator bool() const noexcept {
  return _handle != nullptr;
}
auto BackendInstance::Poll() const -> std::vector<sdlrdp_event> {
  std::array<sdlrdp_event, 256> batch { };
  auto const                    count = sdlrdp_poll(&**this, batch.data(), batch.size());
  return { batch.begin(), batch.begin() + count };
}
auto BackendInstance::Present(std::span<std::uint32_t const> pixels, std::uint32_t width, std::uint32_t height,
                              sdlrdp_rect const& damage) const -> int {
  return sdlrdp_present(&**this, pixels.data(), static_cast<int>(width * sizeof(std::uint32_t)), width, height, &damage,
                        1);
}
}
