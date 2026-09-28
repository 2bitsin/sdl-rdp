#include <sdl-rdp/auth/private-directory.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <filesystem>

namespace sdl_rdp::auth::detail::private_directory {
using sdl_rdp::utilities::Expects;

PrivateDirectory::PrivateDirectory(std::filesystem::path const& path) : _path{ path }, _lock{ Locked(path) } { }
PrivateDirectory::~PrivateDirectory() {
  Unlock(_lock);
}
auto PrivateDirectory::NewFile(std::filesystem::path const& name) const -> Bio {
  return OwnerOnlyFile(Inside(name));
}
auto PrivateDirectory::Secure(std::filesystem::path const& name) const -> void {
  NarrowToOwner(Inside(name));
}
auto PrivateDirectory::Inside(std::filesystem::path const& name) const -> std::filesystem::path {
  auto const bare = name.filename();
  Expects(name == bare, "a bare file name, joined to this directory");
  return _path / name;
}
}
