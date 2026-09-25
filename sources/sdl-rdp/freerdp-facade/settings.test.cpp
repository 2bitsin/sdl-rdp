#include <sdl-rdp/freerdp-facade/settings.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::settings {
namespace {
TEST(Settings, AppliesEveryEntryOfEachKind) {
  Settings const settings(freerdp_settings_new(0));
  ASSERT_TRUE(settings);
  std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 2> const               flags{ {
      { FreeRDP_NSCodec    , true  },
      { FreeRDP_NlaSecurity, false },
  } };
  std::array<std::pair<FreeRDP_Settings_Keys_UInt32, std::uint32_t>, 1> const numbers{ { { FreeRDP_ColorDepth, 16 } } };
  std::array<std::pair<FreeRDP_Settings_Keys_String, std::string_view>, 1> const names{ {
      { FreeRDP_Username, std::string_view{ "alice-and-more" }.substr(0, 5) },
  } };
  EXPECT_EQ(FirstRefused(*settings, flags), std::nullopt);
  EXPECT_EQ(FirstRefused(*settings, numbers), std::nullopt);
  EXPECT_EQ(FirstRefused(*settings, names), std::nullopt);
  EXPECT_TRUE(freerdp_settings_get_bool(settings.get(), FreeRDP_NSCodec));
  EXPECT_FALSE(freerdp_settings_get_bool(settings.get(), FreeRDP_NlaSecurity));
  EXPECT_EQ(freerdp_settings_get_uint32(settings.get(), FreeRDP_ColorDepth), 16U);
  EXPECT_EQ(std::string_view{ freerdp_settings_get_string(settings.get(), FreeRDP_Username) }, "alice");
}
TEST(Settings, StopsAtTheFirstRefusedEntry) {
  Settings const settings(freerdp_settings_new(0));
  ASSERT_TRUE(settings);
  ASSERT_TRUE(Set(*settings, FreeRDP_NSCodec, true));
  std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 2> const flags{ {
      { FreeRDP_BOOL_UNUSED, true  },
      { FreeRDP_NSCodec    , false },
  } };
  EXPECT_EQ(FirstRefused(*settings, flags), FreeRDP_BOOL_UNUSED);
  EXPECT_TRUE(freerdp_settings_get_bool(settings.get(), FreeRDP_NSCodec));
}
TEST(Settings, RefusalNamesTheRefusedKey) {
  EXPECT_EQ(Refusal("codecs", std::optional{ FreeRDP_NSCodec }), "codecs: FreeRDP refused FreeRDP_NSCodec");
  EXPECT_EQ(Refusal("codecs", std::optional<FreeRDP_Settings_Keys_Bool>{ }), "codecs");
}
}
}
