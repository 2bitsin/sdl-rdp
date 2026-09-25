#include <sdl-rdp/settings/refresh.hpp>
#include <sdl-rdp/settings/exceptions.hpp>

#include <gtest/gtest.h>
#include <string_view>
#include <tuple>

namespace sdl_rdp::settings {
TEST(Refresh, ReadsAModeNameOrARate) {
  EXPECT_EQ(Refresh::Parsed("auto-client"), Refresh{ RefreshMode::CLIENT });
  EXPECT_EQ(Refresh::Parsed("auto-client-average"), Refresh{ RefreshMode::CLIENT_AVERAGE });
  EXPECT_EQ(Refresh::Parsed("auto-sender"), Refresh{ RefreshMode::SENDER });
  EXPECT_EQ(Refresh::Parsed(" 90 "), Refresh{ Refresh::Rate{ 90 } });
  for (std::string_view const text : { "", "0", "auto", "FIXED", "90hz", "2147484" })
    EXPECT_EQ(Refresh::Parsed(text), std::nullopt) << text;
}
TEST(Refresh, AnAutomaticModeStartsAtTheDefaultRate) {
  EXPECT_EQ(Refresh{ }.Mode(), RefreshMode::FIXED);
  EXPECT_EQ(Refresh{ RefreshMode::SENDER }.Hz(), Refresh{ }.Hz());
}
TEST(Refresh, TextRoundTrips) {
  for (auto const refresh : { Refresh{ RefreshMode::CLIENT }, Refresh{ Refresh::Rate{ 75 } } })
    EXPECT_EQ(Refresh::_Decode(refresh._Encode()), refresh);
  EXPECT_THROW(std::ignore = Refresh::_Decode("auto"), InvalidSettingValue);
}
TEST(RefreshDeathTest, AFixedModeWithoutARateBreaksTheContract) {
  EXPECT_DEATH(std::ignore = Refresh{ RefreshMode::FIXED }, "names its rate");
}
}
