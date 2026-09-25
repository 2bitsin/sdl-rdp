#include <sdl-rdp/configuration/validation.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <cstdint>
#include <limits>

namespace sdl_rdp::configuration::detail::validation {
using sdl_rdp::picture::Dimensions;
using sdl_rdp::utilities::InvalidArguments;
using sdl_rdp::utilities::OutOfRange;

namespace {
constexpr std::uint32_t MaximumPort         = 65535;
constexpr std::uint32_t MaximumRefreshMode  = 3;
constexpr std::uint32_t MinimumPacedRefresh = 10;
constexpr std::uint32_t LargestMillihertz   = std::numeric_limits<std::int32_t>::max();
// NVENC takes the rate in bits per second as a uint32_t.
constexpr std::uint32_t LargestAvcKbps = std::numeric_limits<std::uint32_t>::max() / 1000;
}
auto Validate(sdlrdp_config const& config) -> void {
  if (config.auth < SDLRDP_AUTH_NONE || config.auth > SDLRDP_AUTH_NLA) throw InvalidChoice{ "authentication mode" };
  std::ignore = Dimensions(config.width, config.height);
  if (config.avc_bitrate_kbps > LargestAvcKbps)
    throw OutOfRange{ "Configured AVC bitrate (kbps)", config.avc_bitrate_kbps, 0, LargestAvcKbps };
  ValidateCodec(config.codec);
  if (config.port > MaximumPort) throw OutOfRange{ "Configured port", config.port, 0, MaximumPort };
}
auto ValidateCodec(sdlrdp_codec codec) -> void {
  if (codec < SDLRDP_CODEC_AUTO || codec > SDLRDP_CODEC_AVC420) throw InvalidChoice{ "codec preference" };
}
auto ValidRefresh(std::uint32_t mode, std::uint32_t ceiling) -> RefreshMode {
  if (mode > MaximumRefreshMode || !ceiling || ceiling > LargestMillihertz / MillihertzPerHz
      || (mode && ceiling < MinimumPacedRefresh))
    throw InvalidArguments{ "refresh", "mode or ceiling" };
  return RefreshMode(mode);
}
}
