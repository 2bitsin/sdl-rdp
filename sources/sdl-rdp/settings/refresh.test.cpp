#include <sdl-rdp/settings/refresh.hpp>
#include <sdl-rdp/settings/exceptions.hpp>

#include <gtest/gtest.h>
#include <string_view>
#include <tuple>

namespace sdl_rdp::settings::detail::refresh {
TEST(Refresh, ReadsAModeNameOrARate) {
  EXPECT_EQ(Refresh::Parsed("auto-client"), Refresh{ RefreshMode::Client });
  EXPECT_EQ(Refresh::Parsed("auto-client-average"), Refresh{ RefreshMode::Average });
  EXPECT_EQ(Refresh::Parsed("auto-sender"), Refresh{ RefreshMode::Sender });
  EXPECT_EQ(Refresh::Parsed(" 90 "), Refresh{ Refresh::Rate{ 90 } });
  for (std::string_view const text : { "", "0", "auto", "FIXED", "90hz", "2147484" })
    EXPECT_EQ(Refresh::Parsed(text), std::nullopt) << text;
}
TEST(Refresh, AnAutomaticModeStartsAtTheDefaultRate) {
  EXPECT_EQ(Refresh{ }.Mode(), RefreshMode::Fixed);
  EXPECT_EQ(Refresh{ RefreshMode::Sender }.Hz(), Refresh{ }.Hz());
}
TEST(Refresh, TextRoundTrips) {
  for (auto const refresh : { Refresh{ RefreshMode::Client }, Refresh{ Refresh::Rate{ 75 } } })
    EXPECT_EQ(Refresh::_Decode(refresh._Encode()), refresh);
  EXPECT_THROW(std::ignore = Refresh::_Decode("auto"), InvalidSettingValue);
}
TEST(RefreshDeathTest, AFixedModeWithoutARateBreaksTheContract) {
  EXPECT_DEATH(std::ignore = Refresh{ RefreshMode::Fixed }, "names its rate");
}
}
