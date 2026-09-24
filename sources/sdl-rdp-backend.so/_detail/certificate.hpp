#pragma once
#include <filesystem>
#include <freerdp/settings.h>

namespace Backend {
struct Credentials {
  std::filesystem::path certificate;
  std::filesystem::path key;
};
std::filesystem::path DefaultCertificateDirectory();
Credentials           EnsureCertificate(std::filesystem::path const& directory);
auto                  InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void;
}
