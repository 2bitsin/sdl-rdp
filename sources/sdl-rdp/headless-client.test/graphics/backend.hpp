#pragma once
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/session/backend.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sdl_rdp::headless_client_test::graphics::detail::backend {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;

class GraphicsBackend : public testing::Test {
protected:
  auto OpenGraphics(std::string_view purpose, std::uint32_t width, std::uint32_t height, Codec codec) -> void;
  auto TearDown()                                                                                     -> void override;
  auto ConnectGraphics(Client& client)                                                                -> void;
  Logs                                        logs;
  std::optional<oxbox::platform::ScratchArea> directory;
  BackendInstance                             backend;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::backend::GraphicsBackend;
}
