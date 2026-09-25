#pragma once
#include "mode.hpp"
#include <sdl-rdp/headless-client.test/backend/events.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <span>
#include <vector>

namespace BackendGate {
class CodecSession : public testing::TestWithParam<Mode>, protected BackendEvents {
protected:
  auto        SetUp()                                                       -> void override;
  auto        ConnectCodec(Client& client)                                  -> void;
  auto        Reopen(std::uint32_t width, std::uint32_t height)             -> void;
  auto        Frame(Client& client, sdlrdp_rect area)                       -> void;
  static auto ThenMotion(sdlrdp_event const& event)                         -> void;
  static auto ThenPointerEvents(std::span<sdlrdp_event const> events)       -> void;
  auto        Input(Client& client)                                         -> void;
  auto        ThenChangedCodec(sdlrdp_codec expected)                       -> void;
  auto        ThenCodecChange(sdlrdp_codec expected, sdlrdp_codec previous) -> void;
  auto        ThenConnected()                                               -> void;
  static auto ThenConnectionDetails(sdlrdp_event const& event)              -> void;
  auto        ThenDisconnected()                                            -> void;
  auto        RecordFrameCost(Client& client, std::uint64_t bytes)          -> void;
  std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(320uz * 200);
};
}
