#pragma once
#include <filesystem>

namespace BackendGate {
class CertificateDirectory {
public:
       CertificateDirectory(CertificateDirectory const&)                 = delete;
       CertificateDirectory(CertificateDirectory&&)                      = delete;
       CertificateDirectory();
       ~CertificateDirectory();
  auto operator = (CertificateDirectory const&) -> CertificateDirectory& = delete;
  auto operator = (CertificateDirectory&&)      -> CertificateDirectory& = delete;
  auto Path() const                             -> std::filesystem::path const&;

private:
  std::filesystem::path path;
};
}
