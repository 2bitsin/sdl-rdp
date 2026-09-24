#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include "../SDL3.so/rdp/SDL_rdpini.hpp"

namespace {
struct Ini {
public:
  struct Warning {
    rdp::IniStatus status;
    std::string    key;
    unsigned       line;
  };
  explicit Ini(std::string const& text) {
    rdp::ParseIni(text, [this](rdp::IniEntry entry) {
      if (!entry.Index())
        warnings.push_back({entry.Status(), std::string(entry.Key()), entry.Line()});
      else
        values[std::string(entry.Key())] = entry.Value();
    });
  }
  std::map<std::string, std::string> values;
  std::vector<Warning>               warnings;
};
TEST(Ini, TrimsBlanksAndPreservesQuotedBlanks) {
  Ini ini(" \tSDL_RDP_PORT \t= 33892 \t\nSDL_RDP_USER = \" alice \" \t\nSDL_RDP_PASSWORD = \"\"\n");
  EXPECT_EQ(ini.values.at("SDL_RDP_PORT"), "33892");
  EXPECT_EQ(ini.values.at("SDL_RDP_USER"), " alice ");
  EXPECT_EQ(ini.values.at("SDL_RDP_PASSWORD"), "");
  EXPECT_TRUE(ini.warnings.empty());
}
TEST(Ini, CommentsSectionsAndBlankLines) {
  Ini ini("\n \t\n # comment\n ; comment\n [server] \nSDL_RDP_PASSWORD = a#b;c=d\n");
  ASSERT_EQ(ini.values.size(), 1u);
  EXPECT_EQ(ini.values.at("SDL_RDP_PASSWORD"), "a#b;c=d");
  EXPECT_TRUE(ini.warnings.empty());
}
TEST(Ini, UnknownAndMalformedLinesReportLineNumbersWithoutValues) {
  Ini ini("# first\nSDL_VIDEO_DRIVER = secret\nSDL_RDP_PASSWORD secret\nSDL_RDP_PORT=1234\n");
  ASSERT_EQ(ini.warnings.size(), 2u);
  EXPECT_EQ(ini.warnings[0].status, rdp::IniStatus::UNKNOWN);
  EXPECT_EQ(ini.warnings[0].key, "SDL_VIDEO_DRIVER");
  EXPECT_EQ(ini.warnings[0].line, 2u);
  EXPECT_EQ(ini.warnings[1].status, rdp::IniStatus::MALFORMED);
  EXPECT_TRUE(ini.warnings[1].key.empty());
  EXPECT_EQ(ini.warnings[1].line, 3u);
  EXPECT_EQ(ini.values.size(), 1u);
}
TEST(Ini, LastDuplicateWinsWithCrlfAndNoFinalNewline) {
  Ini ini("[server]\r\nSDL_RDP_PORT = 1\r\nSDL_RDP_PORT = 33892\r\nSDL_RDP_USER= last \r");
  EXPECT_EQ(ini.values.at("SDL_RDP_PORT"), "33892");
  EXPECT_EQ(ini.values.at("SDL_RDP_USER"), "last");
  EXPECT_TRUE(ini.warnings.empty());
}
TEST(Ini, EveryDriverNameAndIniPathAreRecognized) {
  for (int i = 0; std::cmp_less(i, rdp::SettingNames.size()); ++i) {
    EXPECT_EQ(rdp::SettingIndex(rdp::SettingNames[static_cast<std::size_t>(i)]), i);
  }
  EXPECT_EQ(rdp::SettingIndex("SDL_RDP_INI"), 0);
  EXPECT_EQ(rdp::SettingIndex("SDL_RDP_PORT_SUFFIX"), std::nullopt);
  EXPECT_TRUE(Ini("").values.empty());
}
}

TEST(Ini, FallbackIncludesSlotZeroAndPreservesEmptyValues) {
  rdp::SettingValues values{ };
  values.front() = "chosen.ini";
  EXPECT_EQ(rdp::IniValue(values, "SDL_RDP_INI"), "chosen.ini");
  values.front() = "";
  EXPECT_EQ(rdp::IniValue(values, "SDL_RDP_INI"), "");
  EXPECT_EQ(rdp::IniValue(values, "unknown"), std::nullopt);
}
