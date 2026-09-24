#include "_detail/credentials.hpp"

#include "_detail/contract.hpp"

namespace Backend {
Credentials::Credentials(std::filesystem::path const& directory)
    : _certificate{ directory / "server.crt" }, _key{ directory / "server.key" } {
  Expects(!directory.empty(), "certificate directory is nonempty");
}
std::filesystem::path const& Credentials::Certificate() const noexcept {
  return _certificate;
}
std::filesystem::path const& Credentials::Key() const noexcept {
  return _key;
}
bool Credentials::Exist() const {
  return exists(_certificate) && exists(_key);
}
}
