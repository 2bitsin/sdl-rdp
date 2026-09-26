#include <sdl-rdp/configuration/validation.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <cstdint>
#include <limits>

namespace sdl_rdp::configuration::detail::validation {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::Setup;
using sdl_rdp::picture::Dimensions;
using sdl_rdp::utilities::InvalidArguments;
using sdl_rdp::utilities::OutOfRange;

namespace {
constexpr std::uint32_t MinimumPacedRefresh = 10;
constexpr std::uint32_t LargestMillihertz   = std::numeric_limits<std::int32_t>::max();
// NVENC takes the rate in bits per second as a uint32_t.
constexpr std::uint32_t LargestAvcKbps = std::numeric_limits<std::uint32_t>::max() / 1000;
}
auto Validate(Setup const& config) -> void {
  if (config.auth < AuthMode::None || config.auth > AuthMode::Nla) throw InvalidChoice{ "authentication mode" };
  std::ignore = Dimensions(config.width, config.height);
  if (config.avc_bitrate_kbps > LargestAvcKbps)
    throw OutOfRange{ "Configured AVC bitrate (kbps)", config.avc_bitrate_kbps, 0, LargestAvcKbps };
  ValidateCodec(config.codec);
}
auto ValidateCodec(Codec codec) -> void {
  if (codec < Codec::Auto || codec > Codec::Avc420) throw InvalidChoice{ "codec preference" };
}
auto ValidateRefresh(RefreshMode mode, std::uint32_t ceiling) -> void {
  if (!ceiling || ceiling > LargestMillihertz / MillihertzPerHz
      || (mode != RefreshMode::Fixed && ceiling < MinimumPacedRefresh))
    throw InvalidArguments{ "refresh", "ceiling" };
}
}
