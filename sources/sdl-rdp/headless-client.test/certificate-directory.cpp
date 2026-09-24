#include <sdl-rdp/headless-client.test/certificate-directory.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace BackendGate {
CertificateDirectory::CertificateDirectory() {
  std::array<char, 40> pattern{ };
  std::ranges::copy(std::string("/tmp/sdlrdp-gate-XXXXXX"), pattern.begin());
  auto* result = mkdtemp(pattern.data());
  utilities::Expects(result != nullptr, "temporary directory created");
  path = result;
}
CertificateDirectory::~CertificateDirectory() {
  std::filesystem::remove_all(path);
}
auto CertificateDirectory::Path() const -> std::filesystem::path const& {
  return path;
}
}
