#pragma once

#include <winpr/stream.h>
#include <winpr/wtsapi.h>

namespace Headless {
class SoundClient;
struct SoundProtocol {
private:
  friend class SoundClient;
  static auto Register(CHANNEL_ENTRY_POINTS_EX* points, void* handle)                                  -> BOOL;
  static auto Initialized(void* user, void* /*unused*/, UINT event, void* /*unused*/, UINT /*unused*/) -> void;
  static auto Received(void* user, DWORD /*unused*/, UINT event, void* data, UINT32 size, UINT32 total, UINT32 flags)
      -> void;
  static auto Formats(SoundClient& self, wStream* stream)                                              -> void;
  static auto Wave(SoundClient& self, wStream* stream, unsigned size, bool second)                     -> void;
  static auto Receive(SoundClient& self)                                                               -> void;
  static auto CaptureWave(SoundClient& self)                                                           -> void;
  static auto Dispatch(SoundClient& self, wStream* stream, BYTE type, UINT16 size)                     -> void;
};
}
