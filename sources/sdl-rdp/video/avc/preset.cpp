#include <sdl-rdp/video/avc/preset.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>

#include <winpr/wtypes.h>
// WinPR already supplies the ABI-compatible GUID type.
#define GUID_DEFINED
#include <algorithm>
#include <cstdint>
#include <ffnvcodec/nvEncodeAPI.h>

namespace Backend::Avc {
namespace {
auto ConfigureRate(NV_ENC_RC_PARAMS& rc, std::uint32_t bitrate, std::uint32_t fps) -> void {
  rc.enableLookahead  = 0;
  rc.lookaheadDepth   = 0;
  rc.rateControlMode  = NV_ENC_PARAMS_RC_CBR;
  rc.averageBitRate   = bitrate;
  rc.vbvBufferSize    = Narrowed<std::uint32_t>(
      std::clamp<std::uint64_t>(std::uint64_t{ bitrate } * 2 / fps, 1, UINT32_MAX));
  rc.vbvInitialDelay  = rc.vbvBufferSize;
  rc.zeroReorderDelay = 1;
}
auto ConfigureColour(NV_ENC_CONFIG_H264_VUI_PARAMETERS& vui) -> void {
  vui.videoSignalTypePresentFlag   = 1;
  vui.videoFullRangeFlag           = 1;
  vui.colourDescriptionPresentFlag = 1;
  vui.colourPrimaries              = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
  vui.transferCharacteristics      = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
  vui.colourMatrix                 = NV_ENC_VUI_MATRIX_COEFFS_BT709;
}
auto ConfigureH264(NV_ENC_CONFIG_H264& h264, std::uint32_t fps) -> void {
  h264.chromaFormatIDC = 1;
  h264.level           = NV_ENC_LEVEL_AUTOSELECT;
  h264.idrPeriod       = NVENC_INFINITE_GOPLENGTH;
  h264.repeatSPSPPS    = 1;
  auto refresh = IntraRefreshFor(fps);
  h264.enableIntraRefresh = 1;
  h264.intraRefreshPeriod = refresh.period;
  h264.intraRefreshCnt    = refresh.count;
  ConfigureColour(h264.h264VUIParameters);
}
}
auto ConfigurePreset(NV_ENC_CONFIG& config, std::uint32_t bitrate, std::uint32_t fps) -> void {
  config.profileGUID    = NV_ENC_H264_PROFILE_HIGH_GUID;
  config.gopLength      = NVENC_INFINITE_GOPLENGTH;
  config.frameIntervalP = 1;
  ConfigureRate(config.rcParams, bitrate, fps);
  ConfigureH264(config.encodeCodecConfig.h264Config, fps);
}
} // namespace Backend::Avc
