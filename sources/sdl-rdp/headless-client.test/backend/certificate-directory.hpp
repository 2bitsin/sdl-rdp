#pragma once
#include <filesystem>

namespace sdl_rdp::headless_client_test::backend::detail::certificate_directory {
class CertificateDirectory {
public:
       CertificateDirectory(CertificateDirectory const&)               = delete;
       CertificateDirectory(CertificateDirectory&&)                    = delete;
       CertificateDirectory();
       ~CertificateDirectory();
  auto operator=(CertificateDirectory const&) -> CertificateDirectory& = delete;
  auto operator=(CertificateDirectory&&)      -> CertificateDirectory& = delete;
  auto Path() const                           -> std::filesystem::path const&;

private:
  std::filesystem::path path;
};
}

namespace sdl_rdp::headless_client_test::backend {
using detail::certificate_directory::CertificateDirectory;
}
