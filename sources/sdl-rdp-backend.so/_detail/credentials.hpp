#pragma once
#include <filesystem>

namespace Backend {
class Credentials {
public:
  explicit                     Credentials(std::filesystem::path const& directory);
  std::filesystem::path const& Certificate() const noexcept;
  std::filesystem::path const& Key() const         noexcept;
  bool                         Exist() const;

private:
  std::filesystem::path _certificate;
  std::filesystem::path _key;
};
}
