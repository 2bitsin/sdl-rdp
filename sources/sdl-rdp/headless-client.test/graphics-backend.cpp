#include <sdl-rdp/headless-client.test/graphics-backend.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>

namespace Headless {
auto GraphicsBackend::OpenGraphics(char* pattern, std::uint32_t width, std::uint32_t height, sdlrdp_codec codec)
    -> void {
  auto* path = mkdtemp(pattern);
  ASSERT_NE(path, nullptr);
  directory = path;
  sdlrdp_config config{ "127.0.0.1", 0, directory.c_str(), width, height, 0, Logs::Collect, &logs };
  config.codec = codec;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config));
}
auto GraphicsBackend::TearDown() -> void {
  backend.Close();
  if (!directory.empty()) std::filesystem::remove_all(directory);
}
auto GraphicsBackend::ConnectGraphics(Client& client) -> void {
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(20)))
      << logs.Text(true);
}
}
