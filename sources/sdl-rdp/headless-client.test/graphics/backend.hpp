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
#include <string_view>

namespace sdl_rdp::headless_client_test::graphics::detail::backend {
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;

class GraphicsBackend : public testing::Test {
protected:
  auto OpenGraphics(std::string_view purpose, std::uint32_t width, std::uint32_t height, sdlrdp_codec codec) -> void;
  auto TearDown()                      -> void override;
  auto ConnectGraphics(Client& client) -> void;
  Logs                  logs;
  std::filesystem::path directory;
  BackendInstance       backend;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::backend::GraphicsBackend;
}
