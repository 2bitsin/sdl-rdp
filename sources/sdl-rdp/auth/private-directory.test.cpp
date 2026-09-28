#include <sdl-rdp/auth/private-directory.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <filesystem>
#include <tuple>

namespace sdl_rdp::auth::detail::private_directory {
namespace {
using oxbox::platform::ScratchArea;
}
TEST(PrivateDirectory, NewFileJoinsItsNameToADirectoryWrittenWithATrailingSlash) {
  ScratchArea const      area     { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory{ area.Path() / "private" / ""   };
  EXPECT_NE(directory.NewFile("key"), nullptr);
  EXPECT_TRUE(std::filesystem::exists(area.Path() / "private" / "key"));
}
TEST(PrivateDirectoryDeathTest, NewFileTakesABareName) {
  ScratchArea const      area     { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory{ area.Path() / "private"        };
  EXPECT_DEATH(std::ignore = directory.NewFile(area.Path() / "private" / "key"), "a bare file name");
}
}
