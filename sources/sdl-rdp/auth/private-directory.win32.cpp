#include <sdl-rdp/auth/private-directory.hpp>

#include <sdl-rdp/auth/exceptions.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <openssl/bio.h>
#include <oxbox/utilities/path.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <windows.h>

// aclapi.h declares against the types windows.h defines.
#include <aclapi.h>

namespace sdl_rdp::auth::detail::private_directory {
using oxbox::utilities::PathToString;
using sdl_rdp::utilities::Releases;

namespace {
using FileHandle = std::unique_ptr<void, Releases<::CloseHandle>>;
using AccessList = std::unique_ptr<ACL, Releases<::LocalFree>>;
// abi: GetTokenInformation writes a DWORD, and std::uint32_t* is not PDWORD under MSVC.
using ReturnedLength = std::remove_pointer_t<PDWORD>;
constexpr auto WholeFile     = ~std::uint32_t{ 0 };
constexpr auto ProtectedDacl = DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;

// GetTokenInformation writes TOKEN_USER and, right after it, the SID it points at.
struct TokenUserRecord {
  TOKEN_USER                                   user;
  std::array<std::byte, SECURITY_MAX_SID_SIZE> sid;
};

auto Failure(std::string_view operation) -> std::system_error {
  return { static_cast<int>(::GetLastError()), std::system_category(), std::string(operation) };
}
// The token's user, not OW: an elevated run's owner is Administrators, which an unelevated run holds deny-only.
auto OwnerOnlyAccess() -> AccessList {
  TokenUserRecord record  { };
  ReturnedLength  written = 0;
  if (!::GetTokenInformation(::GetCurrentProcessToken(), TokenUser, &record, sizeof(record), &written))
    throw Failure("Process user");
  EXPLICIT_ACCESS_W access{ };
  access.grfAccessPermissions = FILE_ALL_ACCESS;
  access.grfAccessMode        = SET_ACCESS;
  access.grfInheritance       = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  ::BuildTrusteeWithSidW(&access.Trustee, record.user.User.Sid);
  // abi: built is SetEntriesInAclW's out-parameter, owned by AccessList two lines on.
  PACL       built  = nullptr;
  auto const failed = ::SetEntriesInAclW(1, &access, nullptr, &built);
  AccessList list   { built };
  if (failed != ERROR_SUCCESS) throw std::system_error(static_cast<int>(failed), std::system_category(), "Owner ACL");
  return list;
}
auto Opened(std::filesystem::path const& file) -> FileHandle {
  auto const opened = ::CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (opened == INVALID_HANDLE_VALUE) throw Failure("Private directory lock file");
  return FileHandle{ opened };
}
}
// Windows cannot lock a directory handle, so the lock is a file inside it.
auto PrivateDirectory::Locked(std::filesystem::path const& directory) -> NativeLock {
  std::filesystem::create_directories(directory);
  NarrowToOwner(directory);
  auto       file  = Opened(directory / "lock");
  OVERLAPPED whole { };
  if (!::LockFileEx(file.get(), LOCKFILE_EXCLUSIVE_LOCK, 0, WholeFile, WholeFile, &whole))
    throw Failure("Private directory lock");
  return std::bit_cast<NativeLock>(file.release());
}
auto PrivateDirectory::Unlock(NativeLock lock) noexcept -> void {
  FileHandle const released{ std::bit_cast<void*>(lock) };
}
auto PrivateDirectory::OwnerOnlyFile(std::filesystem::path const& path) -> Bio {
  // The file inherits the directory's owner-only access; OpenSSL opens a UTF-8 name as UTF-16.
  return Bio{ BIO_new_file(PathToString(path).c_str(), "wb") };
}
// A protected DACL replaces what the object had and propagates to the children already inside a directory.
auto PrivateDirectory::NarrowToOwner(std::filesystem::path const& path) -> void {
  auto const list   = OwnerOnlyAccess();
  auto       name   = path.native();
  auto const failed = ::SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, ProtectedDacl, nullptr, nullptr, list.get(),
                                              nullptr);
  if (failed != ERROR_SUCCESS) throw CertificateDirectoryFailed{ PathToString(path) };
}
}
