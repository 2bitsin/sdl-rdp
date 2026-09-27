#pragma once
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string_view>

struct rdp_settings;

namespace sdl_rdp::freerdp_facade::detail::settings {
enum class BoolKey : std::uint8_t {
  AutoReconnectionEnabled,
  DrawAllowDynamicColorFidelity,
  DrawAllowSkipAlpha,
  ExtSecurity,
  FrameMarkerCommandEnabled,
  GfxAVC444,
  GfxH264,
  GfxSendQoeAck,
  IgnoreCertificate,
  NSCodec,
  NlaSecurity,
  RdpSecurity,
  RemoteFxCodec,
  SupportDisplayControl,
  SupportGraphicsPipeline,
  SuppressOutput,
  SurfaceCommandsEnabled,
  SynchronousDynamicChannels,
  TlsSecurity,
  WaitForOutputBufferFlush,
};
enum class NumberKey : std::uint8_t {
  ColorDepth,
  DesktopHeight,
  DesktopWidth,
  FrameAcknowledge,
  GfxCapsFilter,
  KeyboardLayout,
  NSCodecId,
  RemoteFxCodecId,
  RequestedProtocols,
  SelectedProtocol,
  ServerPort,
  SurfaceCommandsSupported,
};
enum class StringKey : std::uint8_t {
  AuthenticationPackageList,
  ClientHostname,
  Domain,
  Password,
  ServerHostname,
  Username,
};
enum class EncryptionLevel : std::uint8_t { ClientCompatible };
struct LargePointerSizes {
  bool up_to_96x96  { };
  bool up_to_384x384{ };
};
// MS-RDPBCGR 2.2.4.2: the auto-reconnect cookie's logon id and sixteen random bytes.
struct ReconnectCookie {
  std::uint32_t                logon_id   { };
  std::array<std::uint8_t, 16> random_bits{ };
};
// SettingsTy is rdp_settings const for SettingsReader, or rdp_settings under SettingsView; settings.cpp has both.
template <typename SettingsTy>
class BasicSettingsReader {
public:
  explicit BasicSettingsReader(SettingsTy& settings) noexcept;
  auto     Get(BoolKey key) const      -> bool;
  auto     Get(NumberKey key) const    -> std::uint32_t;
  auto     Get(StringKey key) const    -> std::optional<std::string_view>;
  auto     LargePointer() const        -> LargePointerSizes;
  auto     AutoReconnectCookie() const -> std::optional<ReconnectCookie>;

protected:
  auto Held() const noexcept -> SettingsTy& {
    return *_settings;
  }

private:
  // isolated: FreeRDP's settings are opaque, and nothing outside this class reads them.
  SettingsTy* _settings;
};
using SettingsReader = BasicSettingsReader<rdp_settings const>;
// A value outside a key's own kind is refused at compile time, a string key taking anything a string_view takes.
template <typename KeyTy, typename ValueTy>
concept Mismatched = !std::same_as<KeyTy, StringKey> || !std::convertible_to<ValueTy, std::string_view>;
class SettingsView : public BasicSettingsReader<rdp_settings> {
public:
  using BasicSettingsReader::BasicSettingsReader;
       operator SettingsReader() const noexcept;
  auto Set(BoolKey key, bool value) const               -> void;
  auto Set(NumberKey key, std::uint32_t value) const    -> void;
  auto Set(StringKey key, std::string_view value) const -> void;
  template <typename KeyTy, typename ValueTy>
    requires Mismatched<KeyTy, ValueTy>
  auto Set(KeyTy key, ValueTy value) const -> void = delete;
  // Sets each (key, value) pair in order.
  auto Apply(std::ranges::input_range auto const& entries) const -> void {
    std::ranges::for_each(entries, [this](auto const& entry) { Set(entry.first, entry.second); });
  }
  auto Wipe(StringKey key) const noexcept                          -> void;
  auto SetEncryptionLevel(EncryptionLevel level) const             -> void;
  auto SetLargePointer(LargePointerSizes sizes) const              -> void;
  auto SetAutoReconnectCookie(ReconnectCookie const& cookie) const -> void;
  auto InstallServerCredentials(std::filesystem::path const& key, std::filesystem::path const& certificate) const
      -> void;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::settings::BoolKey;
using detail::settings::EncryptionLevel;
using detail::settings::LargePointerSizes;
using detail::settings::NumberKey;
using detail::settings::ReconnectCookie;
using detail::settings::SettingsReader;
using detail::settings::SettingsView;
using detail::settings::StringKey;
}
