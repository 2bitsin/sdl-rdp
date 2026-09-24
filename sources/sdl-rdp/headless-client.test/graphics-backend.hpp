#pragma once
#include "backend-instance.hpp"
#include "client.hpp"
#include "logs.hpp"
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

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
  auto AwaitAcknowledgement(Client& client, std::uint64_t sequence)                               -> void;
  Logs                      logs;
  std::filesystem::path     directory;
  Headless::BackendInstance backend;
};
}
