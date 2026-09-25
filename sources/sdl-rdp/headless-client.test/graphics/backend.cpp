#include <sdl-rdp/headless-client.test/graphics/backend.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>

namespace sdl_rdp::headless_client_test::graphics::detail::backend {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
auto GraphicsBackend::OpenGraphics(std::string_view purpose, std::uint32_t width, std::uint32_t height, Codec codec)
    -> void {
  directory.emplace(purpose, "sdl-rdp");
  auto config = LoopbackConfig(directory->Path(), { .width = width, .height = height });
  config.codec = codec;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
}
auto GraphicsBackend::TearDown() -> void {
  backend.Close();
  directory.reset();
}
auto GraphicsBackend::ConnectGraphics(Client& client) -> void {
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(20)))
      << logs.Text(true);
}
}
