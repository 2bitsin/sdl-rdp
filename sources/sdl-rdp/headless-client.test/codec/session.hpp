#pragma once
#include "mode.hpp"
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <gtest/gtest.h>
#include <cstdint>
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
  auto        SetUp()                                              -> void override;
  auto        ConnectCodec(Client& client)                         -> void;
  auto        Reopen(std::uint32_t width, std::uint32_t height)    -> void;
  auto        Frame(Client& client, Rect area)                     -> void;
  static auto ThenMotion(Event const& event)                       -> void;
  static auto ThenPointerEvents(std::span<Event const> events)     -> void;
  auto        Input(Client& client)                                -> void;
  auto        ThenChangedCodec(Codec expected)                     -> void;
  auto        ThenCodecChange(Codec expected, Codec previous)      -> void;
  auto        ThenConnected()                                      -> void;
  static auto ThenConnectionDetails(Event const& event)            -> void;
  auto        ThenDisconnected()                                   -> void;
  auto        RecordFrameCost(Client& client, std::uint64_t bytes) -> void;
  Pixels pixels = Pixels(320uz * 200);
};
}

namespace sdl_rdp::headless_client_test::codec {
using detail::session::CodecSession;
}
