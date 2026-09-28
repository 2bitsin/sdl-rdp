#include <sdl-rdp/auth/private-directory.hpp>

#include <sdl-rdp/utilities/releases.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>
#include <windows.h>

// aclapi.h declares against the types windows.h defines.
#include <aclapi.h>

namespace sdl_rdp::auth::detail::private_directory {
namespace {
using oxbox::platform::ScratchArea;
using sdl_rdp::utilities::Releases;
using OwnedHandle = std::unique_ptr<void, Releases<::CloseHandle>>;
using Descriptor  = std::unique_ptr<void, Releases<::LocalFree>>;
constexpr auto WholeFile = ~std::uint32_t{ 0 };

struct Access {
  bool        protected_list;
  std::size_t entries;
  bool        user_alone;
};
auto UserSid() -> std::vector<std::byte> {
  std::remove_pointer_t<PDWORD> size = 0;
  std::ignore = ::GetTokenInformation(::GetCurrentProcessToken(), TokenUser, nullptr, 0, &size);
  std::vector<std::byte> user(size);
  EXPECT_TRUE(::GetTokenInformation(::GetCurrentProcessToken(), TokenUser, user.data(), size, &size));
  auto const&            token = *reinterpret_cast<TOKEN_USER const*>(user.data());  // abi: the token's own record
  std::vector<std::byte> sid(::GetLengthSid(token.User.Sid));
  EXPECT_TRUE(::CopySid(static_cast<std::uint32_t>(sid.size()), sid.data(), token.User.Sid));
  return sid;
}
// abi: list and descriptor are GetNamedSecurityInfoW's out-parameters, the descriptor owning the list.
auto AccessOf(std::filesystem::path const& path, Access& access) -> void {
  PACL                 list       = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  auto const read = ::GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                            &list, nullptr, &descriptor);
  Descriptor const     owned      { descriptor };
  ASSERT_EQ(read, ERROR_SUCCESS);
  ASSERT_NE(list, nullptr);
  SECURITY_DESCRIPTOR_CONTROL   control  = 0;
  std::remove_pointer_t<PDWORD> revision = 0;
  ASSERT_TRUE(::GetSecurityDescriptorControl(descriptor, &control, &revision));
  void* entry = nullptr;  // abi: GetAce's out-parameter into the list
  ASSERT_TRUE(::GetAce(list, 0, &entry));
  auto const& allowed = *static_cast<ACCESS_ALLOWED_ACE const*>(entry);
  auto* const start   = const_cast<void*>(static_cast<void const*>(&allowed.SidStart));  // abi: EqualSid takes PSID
  auto        sid     = UserSid();
  access = { .protected_list = (control & SE_DACL_PROTECTED) != 0,
             .entries        = list->AceCount,
             .user_alone     = allowed.Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ::EqualSid(start, sid.data()) };
}
auto LockedByAnother(std::filesystem::path const& lock_file) -> bool {
  OwnedHandle const file{ ::CreateFileW(lock_file.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr) };
  OVERLAPPED whole{ };
  if (::LockFileEx(file.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, WholeFile, WholeFile, &whole))
    return !::UnlockFileEx(file.get(), 0, WholeFile, WholeFile, &whole);
  return ::GetLastError() == ERROR_LOCK_VIOLATION;
}
}
// The three DACL cases need real Windows: wine 9.0 stores no DACL and synthesizes one from the mode bits.
TEST(PrivateDirectory, IsForTheUserAloneWithAProtectedList) {
  ScratchArea const      area     { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory{ area.Path() / "private"        };
  Access                 access   { };
  ASSERT_NO_FATAL_FAILURE(AccessOf(area.Path() / "private", access));
  EXPECT_TRUE(access.protected_list);
  EXPECT_EQ(access.entries, std::size_t{ 1 });
  EXPECT_TRUE(access.user_alone);
}
TEST(PrivateDirectory, NewFileInheritsTheUserAloneAccess) {
  ScratchArea const      area     { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory{ area.Path() / "private"        };
  EXPECT_NE(directory.NewFile("key"), nullptr);
  Access access{ };
  ASSERT_NO_FATAL_FAILURE(AccessOf(area.Path() / "private" / "key", access));
  EXPECT_EQ(access.entries, std::size_t{ 1 });
  EXPECT_TRUE(access.user_alone);
}
TEST(PrivateDirectory, SecureReplacesAnExistingKeysAccess) {
  ScratchArea const      area     { "private-directory", "sdl-rdp" };
  PrivateDirectory const directory{ area.Path() / "private"        };
  std::ignore = directory.NewFile("key");
  directory.Secure("key");
  Access access{ };
  ASSERT_NO_FATAL_FAILURE(AccessOf(area.Path() / "private" / "key", access));
  EXPECT_TRUE(access.protected_list);
  EXPECT_TRUE(access.user_alone);
}
TEST(PrivateDirectory, HoldsTheLockAgainstAnotherHandleUntilItEnds) {
  ScratchArea const               area      { "private-directory", "sdl-rdp" };
  auto const                      path      = area.Path() / "private";
  std::optional<PrivateDirectory> directory { std::in_place, path            };
  EXPECT_TRUE(LockedByAnother(path / "lock"));
  directory.reset();
  EXPECT_FALSE(LockedByAnother(path / "lock"));
}
}
