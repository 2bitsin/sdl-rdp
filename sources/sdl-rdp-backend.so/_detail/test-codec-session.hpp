#pragma once
#include "test-backend-events.hpp"
#include "test-mode.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <vector>

namespace BackendGate {
class CodecSession : public testing::TestWithParam<Mode>, protected BackendEvents {
protected:
  void        SetUp() override;
  void        ConnectCodec(Client& client);
  void        Reopen(unsigned width, unsigned height);
  void        Frame(Client& client, sdlrdp_rect area);
  static void ThenMotion(sdlrdp_event const& event);
  static void ThenPointerEvents(std::span<sdlrdp_event const> events);
  void        Input(Client& client);
  void        ThenChangedCodec(sdlrdp_codec expected);
  void        ThenCodecChange(sdlrdp_codec expected, sdlrdp_codec previous);
  void        ThenConnected();
  static void ThenConnectionDetails(sdlrdp_event const& event);
  void        ThenDisconnected();
  void        RecordFrameCost(Client& client, uint64_t bytes);
  std::vector<UINT32> pixels = std::vector<UINT32>(320uz * 200);
};
}
