#pragma once
#include "client.hpp"
#include "handle.hpp"
#include "sdl-rdp-backend.h"
#include "test-logs.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

namespace Headless {
class GraphicsBackend : public testing::Test {
protected:
  void OpenGraphics(char* pattern, unsigned width, unsigned height, sdlrdp_codec codec);
  void TearDown() override;
  void ConnectGraphics(Client& client);
  Logs                                                    logs;
  std::filesystem::path                                   directory;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend  { nullptr, sdlrdp_close };
};
}
