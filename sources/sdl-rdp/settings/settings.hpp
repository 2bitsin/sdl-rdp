#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/settings/aspect.hpp>
#include <sdl-rdp/settings/refresh.hpp>
#include <sdl-rdp/utilities/bounded.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <oxbox/serialization/reflected-scheme.hpp>
#include <oxbox/utilities/text.hpp>
#include <_buildutil/reflect.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// ADL finds a scheme only in the enum's own namespace, and the C ABI declares these enums in the global one.
constexpr auto reflect_scheme([[maybe_unused]] sdlrdp_codec* tag) {
  return ::reflect::enum_scheme<
      ::reflect::enumerator<"SDLRDP_CODEC_AUTO", SDLRDP_CODEC_AUTO, "", "auto">,
      ::reflect::enumerator<"SDLRDP_CODEC_PLANAR", SDLRDP_CODEC_PLANAR, "", "planar">,
      ::reflect::enumerator<"SDLRDP_CODEC_REMOTEFX", SDLRDP_CODEC_REMOTEFX, "", "remotefx">,
      ::reflect::enumerator<"SDLRDP_CODEC_NSCODEC", SDLRDP_CODEC_NSCODEC, "", "nscodec">,
      ::reflect::enumerator<"SDLRDP_CODEC_RAW", SDLRDP_CODEC_RAW, "", "raw">,
      ::reflect::enumerator<"SDLRDP_CODEC_PROGRESSIVE", SDLRDP_CODEC_PROGRESSIVE, "", "progressive">,
      ::reflect::enumerator<"SDLRDP_CODEC_AVC420", SDLRDP_CODEC_AVC420, "", "avc420">>{ };
}
constexpr auto reflect_scheme([[maybe_unused]] sdlrdp_auth* tag) {
  return ::reflect::enum_scheme<::reflect::enumerator<"SDLRDP_AUTH_NONE", SDLRDP_AUTH_NONE, "", "none">,
                                ::reflect::enumerator<"SDLRDP_AUTH_TLS", SDLRDP_AUTH_TLS, "", "tls">,
                                ::reflect::enumerator<"SDLRDP_AUTH_NLA", SDLRDP_AUTH_NLA, "", "nla">>{ };
}

namespace sdl_rdp::settings::detail::settings {
using sdl_rdp::utilities::Expects;

// SDL carries window sizes and millisecond counts as int.
inline constexpr std::uint32_t IntMaximum = std::numeric_limits<std::int32_t>::max();
using sdl_rdp::utilities::Bounded;
using Port         = Bounded<std::uint16_t, 0, std::numeric_limits<std::uint16_t>::max()>;
using Extent       = Bounded<std::uint32_t, 1, IntMaximum>;
using Milliseconds = Bounded<std::uint32_t, 0, IntMaximum>;
using Kilobits     = Bounded<std::uint32_t, 0, std::numeric_limits<std::uint32_t>::max() / 1000>;
// Every field is optional so a key the file leaves out stays absent and the next source answers it.
struct Settings {
  friend constexpr auto reflect_scheme(Settings* tag);
  std::optional<std::string>  backend;
  std::optional<std::string>  bind;
  std::optional<Port>         port;
  std::optional<std::string>  cert_dir;
  std::optional<Extent>       width;
  std::optional<Extent>       height;
  std::optional<Refresh>      refresh;
  std::optional<Aspect>       aspect;
  std::optional<sdlrdp_codec> codec;
  std::optional<Kilobits>     avc_bitrate;
  std::optional<bool>         vsync;
  std::optional<bool>         wait_for_client;
  std::optional<Milliseconds> audio_latency;
  std::optional<Milliseconds> audio_lead;
  std::optional<std::string>  user;
  std::optional<std::string>  password;
  std::optional<std::string>  domain;
  std::optional<sdlrdp_auth>  auth;
  friend auto operator==(Settings const&, Settings const&) -> bool = default;
};
template <oxbox::serialization::HasEnumMap EnumTy>
constexpr auto NameOf(EnumTy value) -> std::string_view {
  auto const name = oxbox::serialization::EnumMapFor<EnumTy>().ToString(value);
  Expects(name.has_value(), "the enumerator has a name");
  return name.value_or("");
}
template <oxbox::serialization::HasEnumMap EnumTy>
auto JoinedNames() -> std::string {
  static constexpr auto map = oxbox::serialization::EnumMapFor<EnumTy>();
  return oxbox::utilities::Joined(map.entries, ", ", &std::pair<EnumTy, std::string_view>::second);
}
}

namespace sdl_rdp::settings {
using detail::settings::Extent;
using detail::settings::JoinedNames;
using detail::settings::Kilobits;
using detail::settings::Milliseconds;
using detail::settings::NameOf;
using detail::settings::Port;
using detail::settings::Settings;
}
