#include <sdl-rdp/freerdp-facade/settings.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/releases.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>

#include <freerdp/constants.h>
#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <freerdp/settings.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <tuple>

namespace sdl_rdp::freerdp_facade::detail::settings {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::Unreachable;

namespace {
template <typename KeyTy, typename NativeTy, std::size_t COUNT>
using Table = std::array<std::pair<KeyTy, NativeTy>, COUNT>;
constexpr Table<BoolKey, FreeRDP_Settings_Keys_Bool, 20>     BoolKeys  { {
    { BoolKey::AutoReconnectionEnabled      , FreeRDP_AutoReconnectionEnabled       },
    { BoolKey::DrawAllowDynamicColorFidelity, FreeRDP_DrawAllowDynamicColorFidelity },
    { BoolKey::DrawAllowSkipAlpha           , FreeRDP_DrawAllowSkipAlpha            },
    { BoolKey::ExtSecurity                  , FreeRDP_ExtSecurity                   },
    { BoolKey::FrameMarkerCommandEnabled    , FreeRDP_FrameMarkerCommandEnabled     },
    { BoolKey::GfxAVC444                    , FreeRDP_GfxAVC444                     },
    { BoolKey::GfxH264                      , FreeRDP_GfxH264                       },
    { BoolKey::GfxSendQoeAck                , FreeRDP_GfxSendQoeAck                 },
    { BoolKey::IgnoreCertificate            , FreeRDP_IgnoreCertificate             },
    { BoolKey::NSCodec                      , FreeRDP_NSCodec                       },
    { BoolKey::NlaSecurity                  , FreeRDP_NlaSecurity                   },
    { BoolKey::RdpSecurity                  , FreeRDP_RdpSecurity                   },
    { BoolKey::RemoteFxCodec                , FreeRDP_RemoteFxCodec                 },
    { BoolKey::SupportDisplayControl        , FreeRDP_SupportDisplayControl         },
    { BoolKey::SupportGraphicsPipeline      , FreeRDP_SupportGraphicsPipeline       },
    { BoolKey::SuppressOutput               , FreeRDP_SuppressOutput                },
    { BoolKey::SurfaceCommandsEnabled       , FreeRDP_SurfaceCommandsEnabled        },
    { BoolKey::SynchronousDynamicChannels   , FreeRDP_SynchronousDynamicChannels    },
    { BoolKey::TlsSecurity                  , FreeRDP_TlsSecurity                   },
    { BoolKey::WaitForOutputBufferFlush     , FreeRDP_WaitForOutputBufferFlush      },
} };
constexpr Table<NumberKey, FreeRDP_Settings_Keys_UInt32, 12> NumberKeys{ {
    { NumberKey::ColorDepth              , FreeRDP_ColorDepth               },
    { NumberKey::DesktopHeight           , FreeRDP_DesktopHeight            },
    { NumberKey::DesktopWidth            , FreeRDP_DesktopWidth             },
    { NumberKey::FrameAcknowledge        , FreeRDP_FrameAcknowledge         },
    { NumberKey::GfxCapsFilter           , FreeRDP_GfxCapsFilter            },
    { NumberKey::KeyboardLayout          , FreeRDP_KeyboardLayout           },
    { NumberKey::NSCodecId               , FreeRDP_NSCodecId                },
    { NumberKey::RemoteFxCodecId         , FreeRDP_RemoteFxCodecId          },
    { NumberKey::RequestedProtocols      , FreeRDP_RequestedProtocols       },
    { NumberKey::SelectedProtocol        , FreeRDP_SelectedProtocol         },
    { NumberKey::ServerPort              , FreeRDP_ServerPort               },
    { NumberKey::SurfaceCommandsSupported, FreeRDP_SurfaceCommandsSupported },
} };
constexpr Table<StringKey, FreeRDP_Settings_Keys_String, 6>  StringKeys{ {
    { StringKey::AuthenticationPackageList, FreeRDP_AuthenticationPackageList },
    { StringKey::ClientHostname           , FreeRDP_ClientHostname            },
    { StringKey::Domain                   , FreeRDP_Domain                    },
    { StringKey::Password                 , FreeRDP_Password                  },
    { StringKey::ServerHostname           , FreeRDP_ServerHostname            },
    { StringKey::Username                 , FreeRDP_Username                  },
} };
// MS-RDPBCGR 2.2.4.2: ARC_SC_PRIVATE_PACKET's cbLen is always 28.
constexpr std::uint32_t CookieLength = 28;
static_assert(NoCodecId == RDP_CODEC_ID_NONE);
static_assert(sizeof(ReconnectCookie::random_bits) == sizeof(ARC_SC_PRIVATE_PACKET::arcRandomBits));
using ServerKey         = std::unique_ptr<rdpPrivateKey, Releases<freerdp_key_free>>;
using ServerCertificate = std::unique_ptr<rdpCertificate, Releases<freerdp_certificate_free>>;

template <typename KeyTy, typename NativeTy, std::size_t COUNT>
consteval auto Dense(Table<KeyTy, NativeTy, COUNT> const& table, KeyTy last) -> bool {
  auto const ordered = [&](std::size_t index) { return std::to_underlying(table[index].first) == index; };
  return std::to_underlying(last) + 1U == COUNT && std::ranges::all_of(std::views::iota(0UZ, COUNT), ordered);
}
static_assert(Dense(BoolKeys, BoolKey::WaitForOutputBufferFlush));
static_assert(Dense(NumberKeys, NumberKey::SurfaceCommandsSupported));
static_assert(Dense(StringKeys, StringKey::Username));

auto Native(BoolKey key) -> FreeRDP_Settings_Keys_Bool {
  return BoolKeys[std::to_underlying(key)].second;
}
auto Native(NumberKey key) -> FreeRDP_Settings_Keys_UInt32 {
  return NumberKeys[std::to_underlying(key)].second;
}
auto Native(StringKey key) -> FreeRDP_Settings_Keys_String {
  return StringKeys[std::to_underlying(key)].second;
}
auto Native(EncryptionLevel level) -> std::uint32_t {
  switch (level) {
  case EncryptionLevel::ClientCompatible: return ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;
  default:                                Unreachable(level);
  }
}
// These pointer setters take ownership despite the generic API's copy documentation.
template <FreeRDP_Settings_Keys_Pointer KEY, typename VTy, auto RELEASE>
auto Adopt(rdpSettings& settings, std::unique_ptr<VTy, Releases<RELEASE>> owned) -> void {
  Expects(owned != nullptr, "the server credential loaded");
  if (!freerdp_settings_set_pointer_len(&settings, KEY, owned.get(), 1)) throw CredentialFailed{ "installation" };
  std::ignore = owned.release();
}
}
template <typename SettingsTy>
BasicSettingsReader<SettingsTy>::BasicSettingsReader(SettingsTy& settings) noexcept : _settings{ &settings } { }
template <typename SettingsTy> auto BasicSettingsReader<SettingsTy>::Get(BoolKey key) const -> bool {
  return freerdp_settings_get_bool(_settings, Native(key));
}
template <typename SettingsTy> auto BasicSettingsReader<SettingsTy>::Get(NumberKey key) const -> std::uint32_t {
  return freerdp_settings_get_uint32(_settings, Native(key));
}
template <typename SettingsTy>
auto BasicSettingsReader<SettingsTy>::Get(StringKey key) const -> std::optional<std::string_view> {
  auto const* value = freerdp_settings_get_string(_settings, Native(key));
  if (value == nullptr) return std::nullopt;
  return std::string_view{ value };
}
template <typename SettingsTy> auto BasicSettingsReader<SettingsTy>::LargePointer() const -> LargePointerSizes {
  auto const flags = freerdp_settings_get_uint32(_settings, FreeRDP_LargePointerFlag);
  return { .up_to_96x96   = (flags & LARGE_POINTER_FLAG_96x96) != 0,
           .up_to_384x384 = (flags & LARGE_POINTER_FLAG_384x384) != 0 };
}
template <typename SettingsTy>
auto BasicSettingsReader<SettingsTy>::AutoReconnectCookie() const -> std::optional<ReconnectCookie> {
  auto const* packet = static_cast<ARC_SC_PRIVATE_PACKET const*>(
      freerdp_settings_get_pointer(_settings, FreeRDP_ServerAutoReconnectCookie));
  if (packet == nullptr || packet->cbLen != CookieLength) return std::nullopt;
  ReconnectCookie cookie{ .logon_id = packet->logonId };
  std::ranges::copy(packet->arcRandomBits, cookie.random_bits.begin());
  return cookie;
}
SettingsView::operator SettingsReader() const noexcept {
  return SettingsReader{ Held() };
}
auto SettingsView::Set(BoolKey key, bool value) const -> void {
  auto const set = freerdp_settings_set_bool(&Held(), Native(key), value);
  Ensures(set, "FreeRDP sets every project key");
}
auto SettingsView::Set(NumberKey key, std::uint32_t value) const -> void {
  auto const set = freerdp_settings_set_uint32(&Held(), Native(key), value);
  Ensures(set, "FreeRDP sets every project key");
}
auto SettingsView::Set(StringKey key, std::string_view value) const -> void {
  if (!freerdp_settings_set_string_len(&Held(), Native(key), value.data(), value.size()))
    throw AllocationFailed{ "Settings string" };
}
auto SettingsView::Wipe(StringKey key) const noexcept -> void {
  auto* value = freerdp_settings_get_string_writable(&Held(), Native(key));
  if (value) utilities::Wipe(std::as_writable_bytes(std::span{ value, std::strlen(value) }));
  // FreeRDP 3.32 include/freerdp/settings.h:553: set_string copies input; nullptr removes the old entry.
  auto const cleared = freerdp_settings_set_string(&Held(), Native(key), nullptr);
  Ensures(cleared, "FreeRDP removes a wiped string");
}
auto SettingsView::SetEncryptionLevel(EncryptionLevel level) const -> void {
  auto const set = freerdp_settings_set_uint32(&Held(), FreeRDP_EncryptionLevel, Native(level));
  Ensures(set, "FreeRDP sets the encryption level");
}
auto SettingsView::SetLargePointer(LargePointerSizes sizes) const -> void {
  auto const flags = (sizes.up_to_96x96 ? LARGE_POINTER_FLAG_96x96 : 0U)
                     | (sizes.up_to_384x384 ? LARGE_POINTER_FLAG_384x384 : 0U);
  auto const set   = freerdp_settings_set_uint32(&Held(), FreeRDP_LargePointerFlag, flags);
  Ensures(set, "FreeRDP sets the pointer sizes");
}
auto SettingsView::SetAutoReconnectCookie(ReconnectCookie const& cookie) const -> void {
  ARC_SC_PRIVATE_PACKET packet{ };
  packet.cbLen   = CookieLength;
  packet.version = AUTO_RECONNECT_VERSION_1;
  packet.logonId = cookie.logon_id;
  std::ranges::copy(cookie.random_bits, packet.arcRandomBits);
  if (!freerdp_settings_set_pointer_len(&Held(), FreeRDP_ServerAutoReconnectCookie, &packet, 1))
    throw AllocationFailed{ "Auto-reconnect cookie" };
}
auto SettingsView::InstallServerCredentials(std::filesystem::path const& key,
                                            std::filesystem::path const& certificate) const -> void {
  ServerKey         loaded_key        { freerdp_key_new_from_file(key.c_str())                 };
  ServerCertificate loaded_certificate{ freerdp_certificate_new_from_file(certificate.c_str()) };
  if (!loaded_key) throw CredentialFailed{ "private key loading" };
  if (!loaded_certificate) throw CredentialFailed{ "certificate loading" };
  Adopt<FreeRDP_RdpServerRsaKey>(Held(), std::move(loaded_key));
  Adopt<FreeRDP_RdpServerCertificate>(Held(), std::move(loaded_certificate));
}
template class BasicSettingsReader<rdp_settings>;
template class BasicSettingsReader<rdp_settings const>;
}
