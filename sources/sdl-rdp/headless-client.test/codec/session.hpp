#pragma once
#include "mode.hpp"
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sdl_rdp::headless_client_test::codec::detail::session {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::BackendEvents;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::link::Event;
using sdl_rdp::utilities::Rect;

class CodecSession : public testing::TestWithParam<Mode>, protected BackendEvents {
protected:
  auto        SetUp()                                                             -> void override;
  auto        ConnectCodec(std::uint32_t width = 320, std::uint32_t height = 200) -> void;
  auto        Reopen(std::uint32_t width, std::uint32_t height)                   -> void;
  auto        Frame(Rect area)                                                    -> void;
  static auto ThenMotion(Event const& event)                                      -> void;
  static auto ThenPointerEvents(std::span<Event const> events)                    -> void;
  auto        Input(Client& sender)                                               -> void;
  auto        ThenChangedCodec(Codec expected)                                    -> void;
  auto        ThenCodecChange(Codec expected, Codec previous)                     -> void;
  auto        ThenConnected()                                                     -> void;
  static auto ThenConnectionDetails(Event const& event)                           -> void;
  auto        ThenDisconnected()                                                  -> void;
  auto        RecordFrameCost(std::uint64_t bytes)                                -> void;
  std::unique_ptr<Client> client;
  Pixels                  pixels = Pixels(320uz * 200);
};
}

namespace sdl_rdp::headless_client_test::codec {
using detail::session::CodecSession;
}
