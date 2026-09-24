#include "_detail/test-graphics-backend.hpp"

#include <chrono>
#include <cstdlib>

namespace Headless {
void GraphicsBackend::OpenGraphics(char* pattern, unsigned width, unsigned height, sdlrdp_codec codec) {
  auto* path = mkdtemp(pattern);
  ASSERT_NE(path, nullptr);
  directory = path;
  sdlrdp_config config{ "127.0.0.1", 0, directory.c_str(), width, height, 0, Logs::Collect, &logs };
  config.codec = codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0) << sdlrdp_last_error();
  backend.reset(handle);
}
void GraphicsBackend::TearDown() {
  backend.reset();
  if (!directory.empty()) std::filesystem::remove_all(directory);
}
void GraphicsBackend::ConnectGraphics(Client& client) {
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(20)))
      << logs.Text(true);
}
}
