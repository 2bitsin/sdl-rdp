#include <sdl-rdp/headless-client.test/backend/certificate-directory.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace sdl_rdp::headless_client_test::backend::detail::certificate_directory {
using sdl_rdp::utilities::Expects;

CertificateDirectory::CertificateDirectory() {
  std::array<char, 40> pattern{ };
  std::ranges::copy(std::string("/tmp/sdlrdp-gate-XXXXXX"), pattern.begin());
  auto* result = mkdtemp(pattern.data());
  Expects(result != nullptr, "temporary directory created");
  path = result;
}
CertificateDirectory::~CertificateDirectory() {
  std::filesystem::remove_all(path);
}
auto CertificateDirectory::Path() const -> std::filesystem::path const& {
  return path;
}
}
