#include "_detail/credentials.hpp"

#include "_detail/contract.hpp"

namespace Backend {
Credentials::Credentials(std::filesystem::path const& directory)
    : _certificate{ directory / "server.crt" }, _key{ directory / "server.key" } {
  Expects(!directory.empty(), "certificate directory is nonempty");
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
