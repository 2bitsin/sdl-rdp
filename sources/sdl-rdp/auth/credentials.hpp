#pragma once
#include <filesystem>

namespace Backend {
class Credentials {
public:
  explicit Credentials(std::filesystem::path const& directory);
  auto     Directory() const noexcept   -> std::filesystem::path const&;
  auto     Certificate() const noexcept -> std::filesystem::path const&;
  auto     Key() const noexcept         -> std::filesystem::path const&;
  auto     Exist() const                -> bool;

private:
  std::filesystem::path _directory;
  std::filesystem::path _certificate;
  std::filesystem::path _key;
};
}
