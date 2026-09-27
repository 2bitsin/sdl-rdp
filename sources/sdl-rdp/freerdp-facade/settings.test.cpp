#include <sdl-rdp/freerdp-facade/settings.hpp>

#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/settings.h>
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::settings {
namespace {
using sdl_rdp::utilities::Releases;
using OwnedSettings = std::unique_ptr<rdpSettings, Releases<freerdp_settings_free>>;
template <typename KeyTy, std::size_t COUNT>
using Spellings = std::array<std::pair<KeyTy, std::string_view>, COUNT>;

constexpr Spellings<BoolKey, 20>   BoolSpellings  { {
    { BoolKey::AutoReconnectionEnabled      , "FreeRDP_AutoReconnectionEnabled"       },
    { BoolKey::DrawAllowDynamicColorFidelity, "FreeRDP_DrawAllowDynamicColorFidelity" },
    { BoolKey::DrawAllowSkipAlpha           , "FreeRDP_DrawAllowSkipAlpha"            },
    { BoolKey::ExtSecurity                  , "FreeRDP_ExtSecurity"                   },
    { BoolKey::FrameMarkerCommandEnabled    , "FreeRDP_FrameMarkerCommandEnabled"     },
    { BoolKey::GfxAVC444                    , "FreeRDP_GfxAVC444"                     },
    { BoolKey::GfxH264                      , "FreeRDP_GfxH264"                       },
    { BoolKey::GfxSendQoeAck                , "FreeRDP_GfxSendQoeAck"                 },
    { BoolKey::IgnoreCertificate            , "FreeRDP_IgnoreCertificate"             },
    { BoolKey::NSCodec                      , "FreeRDP_NSCodec"                       },
    { BoolKey::NlaSecurity                  , "FreeRDP_NlaSecurity"                   },
    { BoolKey::RdpSecurity                  , "FreeRDP_RdpSecurity"                   },
    { BoolKey::RemoteFxCodec                , "FreeRDP_RemoteFxCodec"                 },
    { BoolKey::SupportDisplayControl        , "FreeRDP_SupportDisplayControl"         },
    { BoolKey::SupportGraphicsPipeline      , "FreeRDP_SupportGraphicsPipeline"       },
    { BoolKey::SuppressOutput               , "FreeRDP_SuppressOutput"                },
    { BoolKey::SurfaceCommandsEnabled       , "FreeRDP_SurfaceCommandsEnabled"        },
    { BoolKey::SynchronousDynamicChannels   , "FreeRDP_SynchronousDynamicChannels"    },
    { BoolKey::TlsSecurity                  , "FreeRDP_TlsSecurity"                   },
    { BoolKey::WaitForOutputBufferFlush     , "FreeRDP_WaitForOutputBufferFlush"      },
} };
constexpr Spellings<NumberKey, 12> NumberSpellings{ {
    { NumberKey::ColorDepth              , "FreeRDP_ColorDepth"               },
    { NumberKey::DesktopHeight           , "FreeRDP_DesktopHeight"            },
    { NumberKey::DesktopWidth            , "FreeRDP_DesktopWidth"             },
    { NumberKey::FrameAcknowledge        , "FreeRDP_FrameAcknowledge"         },
    { NumberKey::GfxCapsFilter           , "FreeRDP_GfxCapsFilter"            },
    { NumberKey::KeyboardLayout          , "FreeRDP_KeyboardLayout"           },
    { NumberKey::NSCodecId               , "FreeRDP_NSCodecId"                },
    { NumberKey::RemoteFxCodecId         , "FreeRDP_RemoteFxCodecId"          },
    { NumberKey::RequestedProtocols      , "FreeRDP_RequestedProtocols"       },
    { NumberKey::SelectedProtocol        , "FreeRDP_SelectedProtocol"         },
    { NumberKey::ServerPort              , "FreeRDP_ServerPort"               },
    { NumberKey::SurfaceCommandsSupported, "FreeRDP_SurfaceCommandsSupported" },
} };
constexpr Spellings<StringKey, 6>  StringSpellings{ {
    { StringKey::AuthenticationPackageList, "FreeRDP_AuthenticationPackageList" },
    { StringKey::ClientHostname           , "FreeRDP_ClientHostname"            },
    { StringKey::Domain                   , "FreeRDP_Domain"                    },
    { StringKey::Password                 , "FreeRDP_Password"                  },
    { StringKey::ServerHostname           , "FreeRDP_ServerHostname"            },
    { StringKey::Username                 , "FreeRDP_Username"                  },
} };
auto FreeRdpKey(std::string_view spelling) -> std::ptrdiff_t {
  return freerdp_settings_get_key_for_name(std::string{ spelling }.c_str());
}

class Settings : public testing::Test {
protected:
  OwnedSettings owned{ freerdp_settings_new(0) };
  SettingsView  view { *owned                  };
};
TEST_F(Settings, EveryKeyReachesTheFreeRdpKeyOfItsName) {
  for (auto const& [key, spelling] : BoolSpellings) {
    auto const native = static_cast<FreeRDP_Settings_Keys_Bool>(FreeRdpKey(spelling));
    view.Set(key, !freerdp_settings_get_bool(owned.get(), native));
    EXPECT_EQ(freerdp_settings_get_bool(owned.get(), native), view.Get(key)) << spelling;
    view.Set(key, !view.Get(key));
    EXPECT_EQ(freerdp_settings_get_bool(owned.get(), native), view.Get(key)) << spelling;
  }
  for (auto const& [index, entry] : NumberSpellings | std::views::enumerate) {
    auto const value = static_cast<std::uint32_t>(1000 + index);
    view.Set(entry.first, value);
    auto const native = static_cast<FreeRDP_Settings_Keys_UInt32>(FreeRdpKey(entry.second));
    EXPECT_EQ(freerdp_settings_get_uint32(owned.get(), native), value) << entry.second;
  }
  for (auto const& [key, spelling] : StringSpellings) {
    view.Set(key, spelling);
    auto const native = static_cast<FreeRDP_Settings_Keys_String>(FreeRdpKey(spelling));
    EXPECT_EQ(std::string_view{ freerdp_settings_get_string(owned.get(), native) }, spelling);
  }
}
TEST_F(Settings, ApplySetsEveryEntryAndReadersSeeIt) {
  std::array<std::pair<StringKey, std::string_view>, 1> const names{ {
      { StringKey::Username, std::string_view{ "alice-and-more" }.substr(0, 5) },
  } };
  view.Apply(names);
  SettingsReader const reader = view;
  EXPECT_EQ(reader.Get(StringKey::Username), "alice");
}
TEST_F(Settings, WipeRemovesTheString) {
  view.Set(StringKey::Password, "secret");
  view.Wipe(StringKey::Password);
  EXPECT_EQ(view.Get(StringKey::Password), std::nullopt);
}
TEST_F(Settings, EnumeratedValuesReachFreeRdpAsItsFlags) {
  view.SetEncryptionLevel(EncryptionLevel::ClientCompatible);
  view.SetLargePointer({ .up_to_96x96 = false, .up_to_384x384 = true });
  EXPECT_EQ(freerdp_settings_get_uint32(owned.get(), FreeRDP_EncryptionLevel), ENCRYPTION_LEVEL_CLIENT_COMPATIBLE);
  EXPECT_EQ(freerdp_settings_get_uint32(owned.get(), FreeRDP_LargePointerFlag), LARGE_POINTER_FLAG_384x384);
  EXPECT_FALSE(view.LargePointer().up_to_96x96);
  EXPECT_TRUE(view.LargePointer().up_to_384x384);
}
TEST_F(Settings, AutoReconnectCookieReadsBackOnlyOnceSet) {
  EXPECT_EQ(view.AutoReconnectCookie(), std::nullopt);
  ReconnectCookie const cookie{ .logon_id    = 7,
                                .random_bits = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } };
  view.SetAutoReconnectCookie(cookie);
  auto const read = view.AutoReconnectCookie().value_or(ReconnectCookie{ });
  EXPECT_EQ(read.logon_id, cookie.logon_id);
  EXPECT_EQ(read.random_bits, cookie.random_bits);
}
}
}
