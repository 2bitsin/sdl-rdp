#pragma once
#include <sdl-rdp/headless-client.test/client/forward.hpp>

#include <freerdp/svc.h>
#include <winpr/stream.h>
#include <winpr/wtsapi.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::headless_client_test::client::detail::sound_protocol {

struct SoundProtocol {
private:
  friend SoundClient;
  static auto EntryPoint() -> PVIRTUALCHANNELENTRYEX;
  static auto Register(SoundClient& self, CHANNEL_ENTRY_POINTS_EX const& points, void* handle)    -> bool;
  static auto Initialized(SoundClient& self, std::uint32_t event)                                 -> void;
  static auto Received(SoundClient& self, std::span<std::byte const> bytes, std::size_t total, std::uint32_t flags)
      -> void;
  static auto Formats(SoundClient& self, wStream* stream)                                         -> void;
  static auto Wave(SoundClient& self, wStream* stream, std::uint32_t size, bool second)           -> void;
  static auto Receive(SoundClient& self)                                                          -> void;
  static auto CaptureWave(SoundClient& self)                                                      -> void;
  static auto Train(SoundClient& self)                                                            -> void;
  static auto Dispatch(SoundClient& self, wStream* stream, std::uint8_t type, std::uint16_t size) -> void;
};
}

namespace sdl_rdp::headless_client_test::client {
using detail::sound_protocol::SoundProtocol;
}
