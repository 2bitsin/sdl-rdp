#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
extern "C" {
#include "../SDL3.so/rdp/SDL_rdpini.h"
}

namespace {
struct Ini {
public:
  struct Warning {
    int         index;
    std::string key;
    unsigned    line;
  };
  explicit Ini(std::string text) {
    SDL_RDP_IniParse(
        text.data(),
        [](void* raw, int index, char const* key, char const* value, unsigned line) {
          auto& self = *static_cast<Ini*>(raw);
          if (index < 0)
            self.warnings.push_back({ index, key, line });
          else
            self.values[key] = value;
        },
        this);
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
  EXPECT_EQ(ini.warnings[0].index, -1);
  EXPECT_EQ(ini.warnings[0].key, "SDL_VIDEO_DRIVER");
  EXPECT_EQ(ini.warnings[0].line, 2u);
  EXPECT_EQ(ini.warnings[1].index, -2);
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
  for (int i = 0; i < SDL_RDP_SETTING_COUNT; ++i) {
    EXPECT_EQ(SDL_RDP_IniIndex(SDL_RDP_SettingNames[i]), i);
  }
  EXPECT_EQ(SDL_RDP_IniIndex("SDL_RDP_INI"), SDL_RDP_SETTING_INI);
  EXPECT_EQ(SDL_RDP_IniIndex("SDL_RDP_PORT_SUFFIX"), -1);
  EXPECT_TRUE(Ini("").values.empty());
}
}
