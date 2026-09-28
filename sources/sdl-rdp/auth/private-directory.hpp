#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <filesystem>

namespace sdl_rdp::auth::detail::private_directory {
using sdl_rdp::freerdp_facade::Bio;
using sdl_rdp::utilities::Pinned;

// A directory only its owner may enter, created if absent and locked against other processes while this lives.
class PrivateDirectory : private Pinned {
public:
  explicit PrivateDirectory(std::filesystem::path const& path);
           ~PrivateDirectory();
  // A bare file name inside the directory, truncated if it exists; only the owner may read it.
  [[nodiscard]] auto NewFile(std::filesystem::path const& name) const -> Bio;
  // Narrows a file already in the directory to the owner alone.
  auto Secure(std::filesystem::path const& name) const -> void;

private:
  // A descriptor on POSIX, a HANDLE on Windows; closing it releases the lock.
  enum class NativeLock : std::uintptr_t { };
  [[nodiscard]] auto        Inside(std::filesystem::path const& name) const  -> std::filesystem::path;
  [[nodiscard]] static auto Locked(std::filesystem::path const& directory)   -> NativeLock;
  static auto               Unlock(NativeLock lock) noexcept                 -> void;
  [[nodiscard]] static auto OwnerOnlyFile(std::filesystem::path const& path) -> Bio;
  static auto               NarrowToOwner(std::filesystem::path const& path) -> void;
  std::filesystem::path _path;
  NativeLock            _lock;
};
}

namespace sdl_rdp::auth {
using detail::private_directory::PrivateDirectory;
}
