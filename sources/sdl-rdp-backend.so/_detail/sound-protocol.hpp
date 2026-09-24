#pragma once

#include <winpr/stream.h>
#include <winpr/wtsapi.h>

namespace Headless {
class SoundClient;
struct SoundProtocol {
private:
  friend class SoundClient;
  static BOOL Register(CHANNEL_ENTRY_POINTS_EX* points, void* handle);
  static void Initialized(void* user, void* /*unused*/, UINT event, void* /*unused*/, UINT /*unused*/);
  static void Received(void* user, DWORD /*unused*/, UINT event, void* data, UINT32 size, UINT32 total, UINT32 flags);
  static void Formats(SoundClient& self, wStream* stream);
  static void Wave(SoundClient& self, wStream* stream, unsigned size, bool second);
  static void Receive(SoundClient& self);
  static void CaptureWave(SoundClient& self);
  static void Dispatch(SoundClient& self, wStream* stream, BYTE type, UINT16 size);
};
}
