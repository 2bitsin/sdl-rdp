#pragma once
#include "backend-events.hpp"
#include "mode.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <span>
#include <vector>

namespace BackendGate {
class CodecSession : public testing::TestWithParam<Mode>, protected BackendEvents {
protected:
  auto        SetUp()                                                       -> void override;
  auto        ConnectCodec(Client& client)                                  -> void;
  auto        Reopen(unsigned width, unsigned height)                       -> void;
  auto        Frame(Client& client, sdlrdp_rect area)                       -> void;
  static auto ThenMotion(sdlrdp_event const& event)                         -> void;
  static auto ThenPointerEvents(std::span<sdlrdp_event const> events)       -> void;
  auto        Input(Client& client)                                         -> void;
  auto        ThenChangedCodec(sdlrdp_codec expected)                       -> void;
  auto        ThenCodecChange(sdlrdp_codec expected, sdlrdp_codec previous) -> void;
  auto        ThenConnected()                                               -> void;
  static auto ThenConnectionDetails(sdlrdp_event const& event)              -> void;
  auto        ThenDisconnected()                                            -> void;
  auto        RecordFrameCost(Client& client, uint64_t bytes)               -> void;
  std::vector<UINT32> pixels = std::vector<UINT32>(320uz * 200);
};
}
