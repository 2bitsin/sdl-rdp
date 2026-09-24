#pragma once
#include "contract.hpp"

#include <algorithm>
#include <array>
#include <freerdp/channels/rdpgfx.h>
#include <ranges>
#include <span>
#include <winpr/sysinfo.h>

namespace Backend {
inline constexpr std::array versions             { RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
                                                   RDPGFX_CAPVERSION_101, RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_103,
                                                   RDPGFX_CAPVERSION_104, RDPGFX_CAPVERSION_105, RDPGFX_CAPVERSION_106,
                                                   RDPGFX_CAPVERSION_106_ERR, RDPGFX_CAPVERSION_107 };
inline constexpr UINT32     Version101DataLength = 16;
inline constexpr UINT32     FlagsDataLength      = 4;
inline bool AllowsAvc(RDPGFX_CAPSET const& cap) {
  return cap.version == RDPGFX_CAPVERSION_81
             ? (cap.flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) != 0
             : cap.version >= RDPGFX_CAPVERSION_10 && !(cap.flags & RDPGFX_CAPS_FLAG_AVC_DISABLED);
}
inline auto CapabilityDataLength(uint32_t version) -> uint32_t {
  return version == RDPGFX_CAPVERSION_101 ? Version101DataLength : FlagsDataLength;
}
inline auto Acceptable(RDPGFX_CAPSET const& cap) -> bool {
  return std::ranges::contains(versions, cap.version) && cap.length >= CapabilityDataLength(cap.version);
}
inline auto Newest(std::span<RDPGFX_CAPSET const> caps) -> RDPGFX_CAPSET {
  auto       acceptable = caps | std::views::filter(Acceptable);
  auto const newest     = std::ranges::max_element(acceptable, { }, &RDPGFX_CAPSET::version);
  return newest == acceptable.end() ? RDPGFX_CAPSET{ } : *newest;
}
inline auto AnsweredFlags(RDPGFX_CAPSET const& cap, bool avc) -> uint32_t {
  if (cap.version == RDPGFX_CAPVERSION_101) return 0;
  auto const kept =
      cap.flags & (RDPGFX_CAPS_FLAG_THINCLIENT | RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE);
  if (cap.version == RDPGFX_CAPVERSION_81) return avc ? kept | RDPGFX_CAPS_FLAG_AVC420_ENABLED : kept;
  return cap.version >= RDPGFX_CAPVERSION_10 && !avc ? kept | RDPGFX_CAPS_FLAG_AVC_DISABLED : kept;
}
inline auto SelectCapability(std::span<RDPGFX_CAPSET const> caps, bool avc_available = false) -> RDPGFX_CAPSET {
  auto selected = Newest(caps);
  if (!selected.version) return selected;
  selected.length = CapabilityDataLength(selected.version);
  selected.flags  = AnsweredFlags(selected, avc_available && AllowsAvc(selected));
  return selected;
}
inline UINT32 FrameTimestamp(SYSTEMTIME const& time) {
  constexpr unsigned HourShift       = 22;
  constexpr unsigned MinuteShift     = 16;
  constexpr unsigned SecondShift     = 10;
  constexpr unsigned HourBits        = 5;
  constexpr unsigned MinuteBits      = 6;
  constexpr unsigned SecondBits      = 6;
  constexpr unsigned MillisecondBits = 10;
  utilities::Expects(time.wHour < (1u << HourBits), "MS-RDPEGFX 2.2.2.11 timestamp fields fit");
  utilities::Expects(time.wMinute < (1u << MinuteBits), "MS-RDPEGFX 2.2.2.11 timestamp fields fit");
  utilities::Expects(time.wSecond < (1u << SecondBits), "MS-RDPEGFX 2.2.2.11 timestamp fields fit");
  utilities::Expects(time.wMilliseconds < (1u << MillisecondBits), "MS-RDPEGFX 2.2.2.11 timestamp fields fit");
  return (UINT32(time.wHour) << HourShift) | (UINT32(time.wMinute) << MinuteShift) |
         (UINT32(time.wSecond) << SecondShift) | time.wMilliseconds;
}
}
