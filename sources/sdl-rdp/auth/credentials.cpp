#include <sdl-rdp/auth/credentials.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::auth::detail::credentials {
using sdl_rdp::utilities::Expects;

Credentials::Credentials(std::filesystem::path const& directory)
    : _directory{ directory }, _certificate{ directory / "server.crt" }, _key{ directory / "server.key" } {
  Expects(!directory.empty(), "certificate directory is nonempty");
}
auto Credentials::Directory() const noexcept -> std::filesystem::path const& {
  return _directory;
}
auto Credentials::Certificate() const noexcept -> std::filesystem::path const& {
  return _certificate;
}
auto Credentials::Key() const noexcept -> std::filesystem::path const& {
  return _key;
}
auto Credentials::Exist() const -> bool {
  return exists(_certificate) && exists(_key);
}
}
