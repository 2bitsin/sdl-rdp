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
  // abi: VIRTUALCHANNELENTRYEX, BOOL is int
  static auto Entry(CHANNEL_ENTRY_POINTS_EX* points, void* handle) -> int;
  // abi: CHANNEL_INIT_EVENT_EX_FN, UINT is uint32_t
  static auto Initialized(void* user, void* init, std::uint32_t event, void* data, std::uint32_t size) -> void;
  // abi: CHANNEL_OPEN_EVENT_EX_FN, DWORD, UINT and UINT32 are uint32_t
  static auto Opened(void* user, std::uint32_t open, std::uint32_t event, void* data, std::uint32_t size,
                     std::uint32_t total, std::uint32_t flags) -> void;
  static auto Received(SoundClient& self, std::span<std::byte const> bytes, std::size_t total, std::uint32_t flags)
      -> void;
  static auto Formats(SoundClient& self, wStream& stream)                                         -> void;
  static auto Wave(SoundClient& self, wStream& stream, std::uint32_t size, bool second)           -> void;
  static auto Receive(SoundClient& self)                                                          -> void;
  static auto CaptureWave(SoundClient& self)                                                      -> void;
  static auto Train(SoundClient& self)                                                            -> void;
  static auto Dispatch(SoundClient& self, wStream& stream, std::uint8_t type, std::uint16_t size) -> void;
};
}

namespace sdl_rdp::headless_client_test::client {
using detail::sound_protocol::SoundProtocol;
}
