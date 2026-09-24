#pragma once
#include <filesystem>

namespace BackendGate {
class CertificateDirectory {
public:
                               CertificateDirectory(CertificateDirectory const&) = delete;
                               CertificateDirectory(CertificateDirectory&&)      = delete;
                               CertificateDirectory();
                               ~CertificateDirectory();
  CertificateDirectory&        operator = (CertificateDirectory const&)          = delete;
  CertificateDirectory&        operator = (CertificateDirectory&&)               = delete;
  std::filesystem::path const& Path() const;

private:
  std::filesystem::path path;
};
}
