#pragma once
#include "headless-gfx.hpp"
#include "state.hpp"
#include "test-logs.hpp"

#include <filesystem>
#include <gtest/gtest.h>
namespace Headless {
class GraphicsBackend : public testing::Test {
protected:
  void OpenGraphics(char* pattern, unsigned width, unsigned height, sdlrdp_codec codec) {
    auto* path = mkdtemp(pattern);
    ASSERT_NE(path, nullptr);
    directory = path;
    sdlrdp_config config{ "127.0.0.1", 0, directory.c_str(), width, height, 0, Logs::Collect, &logs };
    config.codec = codec;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0) << sdlrdp_last_error();
    backend.reset(handle);
  }
  void TearDown() override {
    backend.reset();
    if (!directory.empty()) std::filesystem::remove_all(directory);
  }
  void ConnectGraphics(Client& client) {
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  }
  Logs                                                    logs;
  std::filesystem::path                                   directory;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend  { nullptr, sdlrdp_close };
};
}
