#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/session/handle.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace Headless {
class GraphicsBackend : public testing::Test {
protected:
  auto OpenGraphics(char* pattern, std::uint32_t width, std::uint32_t height, sdlrdp_codec codec) -> void;
  auto TearDown()                                                                                 -> void override;
  auto ConnectGraphics(Client& client)                                                            -> void;
  Logs                      logs;
  std::filesystem::path     directory;
  Headless::BackendInstance backend;
};
}
