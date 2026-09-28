#include <sdl-rdp/auth/private-directory.hpp>

#include <sdl-rdp/auth/exceptions.hpp>
#include <sdl-rdp/utilities/descriptor.posix.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <openssl/bio.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <sys/stat.h>
#include <tuple>
#include <utility>

namespace sdl_rdp::auth::detail::private_directory {
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::SystemCall;

namespace {
constexpr mode_t OwnerOnlyDirectory = 0700;
constexpr mode_t OwnerReadWrite     = 0600;

auto OwnerOnly(std::filesystem::path const& directory) -> void {
  if (!directory.parent_path().empty()) std::filesystem::create_directories(directory.parent_path());
  if (::mkdir(directory.c_str(), OwnerOnlyDirectory) != 0 && errno != EEXIST)
    throw CertificateDirectoryFailed{ directory.native() };
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
}
}
auto PrivateDirectory::Locked(std::filesystem::path const& directory) -> NativeLock {
  OwnerOnly(directory);
  Descriptor locked{ SystemCall(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), "Private directory") };
  SystemCall(::flock(locked.Get(), LOCK_EX), "Private directory lock");
  return NativeLock{ static_cast<std::uintptr_t>(locked.Release()) };
}
auto PrivateDirectory::Unlock(NativeLock lock) noexcept -> void {
  Descriptor const released{ Narrowed<int>(std::to_underlying(lock)) };
}
auto PrivateDirectory::OwnerOnlyFile(std::filesystem::path const& path) -> Bio {
  auto const opened = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, OwnerReadWrite);
  if (opened < 0) return Bio{ };
  Descriptor file{ opened                            };
  Bio        bio { BIO_new_fd(file.Get(), BIO_CLOSE) };
  if (bio) std::ignore = file.Release();
  return bio;
}
auto PrivateDirectory::NarrowToOwner(std::filesystem::path const& path) -> void {
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
}
