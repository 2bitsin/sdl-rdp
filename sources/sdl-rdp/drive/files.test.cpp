#include <sdl-rdp/drive/files.hpp>

#include <sdl-rdp/drive/exceptions.hpp>

#include <gtest/gtest.h>
#include <tuple>

namespace sdl_rdp::drive::detail::files {
TEST(DriveFiles, NoChannelListsNoDrives) {
  EXPECT_TRUE(DriveFiles{ nullptr }.List().empty());
}
TEST(DriveFiles, NoChannelRefusesEveryPathOperation) {
  DriveFiles const files{ nullptr };
  EXPECT_THROW(std::ignore = files.Open(1, "/a", { .read = true }, FileKind::File), NoDriveChannel);
  EXPECT_THROW(std::ignore = files.Stat(1, "/a"), NoDriveChannel);
  EXPECT_THROW(std::ignore = files.Enumerate(1, "/", 0, 1), NoDriveChannel);
  EXPECT_THROW(files.MakeDirectory(1, "/a"), NoDriveChannel);
  EXPECT_THROW(files.Remove(1, "/a"), NoDriveChannel);
  EXPECT_THROW(files.Rename(1, "/a", "/b"), NoDriveChannel);
}
}
