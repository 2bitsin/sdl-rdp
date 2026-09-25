#include <sdl-rdp/drive/files.hpp>

#include <sdl-rdp/drive/exceptions.hpp>

#include <gtest/gtest.h>
#include <array>
#include <span>

namespace sdl_rdp::drive {
TEST(DriveFiles, NoChannelListsNoDrives) {
  std::array<sdlrdp_drive, 2> out{ };
  EXPECT_EQ(DriveFiles{ nullptr }.List(out), 0);
  EXPECT_EQ(DriveFiles{ nullptr }.List({ }), 0);
}
TEST(DriveFiles, NoChannelRefusesEveryPathOperation) {
  DriveFiles const files{ nullptr };
  EXPECT_THROW(std::ignore = files.Open(1, "/a", SDLRDP_FILE_READ), NoDriveChannel);
  EXPECT_THROW(std::ignore = files.Stat(1, "/a"), NoDriveChannel);
  EXPECT_THROW(std::ignore = files.Enumerate(1, "/", 0, { }), NoDriveChannel);
  EXPECT_THROW(files.MakeDirectory(1, "/a"), NoDriveChannel);
  EXPECT_THROW(files.Remove(1, "/a"), NoDriveChannel);
  EXPECT_THROW(files.Rename(1, "/a", "/b"), NoDriveChannel);
}
}
