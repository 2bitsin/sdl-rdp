#include <sdl-rdp/auth/private-directory.hpp>

#include <sdl-rdp/utilities/descriptor.posix.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <sys/file.h>
#include <tuple>
#include <utility>

namespace sdl_rdp::auth::detail::private_directory {
namespace {
using oxbox::platform::ScratchArea;
using sdl_rdp::utilities::Descriptor;
using std::filesystem::perms;

auto Permissions(std::filesystem::path const& path) -> perms {
  return std::filesystem::status(path).permissions() & perms::all;
}
}
TEST(PrivateDirectory, CreatesTheDirectoryAndItsParentsForTheOwnerAlone) {
  ScratchArea const      area      { "private-directory", "sdl-rdp" };
  auto const             path      = area.Path() / "parent" / "private";
  PrivateDirectory const directory { path                           };
  EXPECT_EQ(Permissions(path), perms::owner_all);
}
TEST(PrivateDirectory, NarrowsAnExistingDirectoryToTheOwner) {
  ScratchArea const area { "private-directory", "sdl-rdp" };
  auto const        path = area.Path() / "private";
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, perms::owner_all | perms::group_all | perms::others_read);
  PrivateDirectory const directory{ path };
  EXPECT_EQ(Permissions(path), perms::owner_all);
}
TEST(PrivateDirectory, HoldsTheLockUntilItEnds) {
  ScratchArea const               area      { "private-directory", "sdl-rdp"                           };
  auto const                      path      = area.Path() / "private";
  std::optional<PrivateDirectory> directory { std::in_place, path                                      };
  Descriptor const                other     { ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC) };
  EXPECT_EQ(::flock(other.Get(), LOCK_EX | LOCK_NB), -1);
  EXPECT_EQ(errno, EWOULDBLOCK);
  directory.reset();
  EXPECT_EQ(::flock(other.Get(), LOCK_EX | LOCK_NB), 0);
}
TEST(PrivateDirectory, NewFileIsForTheOwnerAlone) {
  ScratchArea const      area     { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory{ area.Path() / "private"        };
  EXPECT_NE(directory.NewFile("key"), nullptr);
  EXPECT_EQ(Permissions(area.Path() / "private" / "key"), perms::owner_read | perms::owner_write);
}
TEST(PrivateDirectory, SecureNarrowsAnExistingFileToTheOwner) {
  ScratchArea const      area      { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory { area.Path() / "private"        };
  auto const             file      = area.Path() / "private" / "key";
  std::ignore = directory.NewFile("key");
  std::filesystem::permissions(file, perms::owner_read | perms::owner_write | perms::group_read | perms::others_read);
  directory.Secure("key");
  EXPECT_EQ(Permissions(file), perms::owner_read | perms::owner_write);
}
}
