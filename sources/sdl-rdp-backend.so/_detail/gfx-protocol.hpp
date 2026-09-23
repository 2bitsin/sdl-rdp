#pragma once
#include "contract.hpp"
#include <freerdp/channels/rdpgfx.h>
#include <winpr/sysinfo.h>
#include <algorithm>
#include <array>
#include <span>

namespace Backend {
inline RDPGFX_CAPSET SelectCapability(std::span<RDPGFX_CAPSET const> caps, bool avc_available = false)
{
  constexpr std::array versions{RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
    RDPGFX_CAPVERSION_101, RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_103, RDPGFX_CAPVERSION_104,
    RDPGFX_CAPVERSION_105, RDPGFX_CAPVERSION_106, RDPGFX_CAPVERSION_106_ERR, RDPGFX_CAPVERSION_107};
  constexpr UINT32 Version101DataLength = 16, FlagsDataLength = 4;
  RDPGFX_CAPSET selected{};
  for (auto const& cap : caps) {
    if (std::ranges::find(versions, cap.version) != versions.end() && cap.version > selected.version
        && cap.length >= (cap.version == RDPGFX_CAPVERSION_101 ? Version101DataLength : FlagsDataLength))
      selected = cap;
  }
  if (!selected.version) return selected;
  selected.length = selected.version == RDPGFX_CAPVERSION_101 ? Version101DataLength : FlagsDataLength;
  bool avc = avc_available && (selected.version == RDPGFX_CAPVERSION_81
    ? (selected.flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) != 0
    : selected.version >= RDPGFX_CAPVERSION_10 && !(selected.flags & RDPGFX_CAPS_FLAG_AVC_DISABLED));
  selected.flags &= RDPGFX_CAPS_FLAG_THINCLIENT | RDPGFX_CAPS_FLAG_SMALL_CACHE
    | RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE;
  if (selected.version == RDPGFX_CAPVERSION_101) selected.flags = 0;
  else if (selected.version >= RDPGFX_CAPVERSION_10 && !avc) selected.flags |= RDPGFX_CAPS_FLAG_AVC_DISABLED;
  if (avc && selected.version == RDPGFX_CAPVERSION_81) selected.flags |= RDPGFX_CAPS_FLAG_AVC420_ENABLED;
  return selected;
}
inline UINT32 FrameTimestamp(SYSTEMTIME const& time)
{
  constexpr unsigned HourShift = 22, MinuteShift = 16, SecondShift = 10;
  constexpr unsigned HourBits = 5, MinuteBits = 6, SecondBits = 6, MillisecondBits = 10;
  utilities::Expects(time.wHour < (1u << HourBits) && time.wMinute < (1u << MinuteBits)
    && time.wSecond < (1u << SecondBits) && time.wMilliseconds < (1u << MillisecondBits),
    "MS-RDPEGFX 2.2.2.11 timestamp fields fit");
  return (UINT32(time.wHour) << HourShift) | (UINT32(time.wMinute) << MinuteShift)
    | (UINT32(time.wSecond) << SecondShift) | time.wMilliseconds;
}
}
