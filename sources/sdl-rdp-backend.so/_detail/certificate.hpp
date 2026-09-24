#pragma once
#include "credentials.hpp"

#include <filesystem>
#include <freerdp/settings.h>

namespace Backend {
std::filesystem::path DefaultCertificateDirectory();
Credentials           EnsureCertificate(std::filesystem::path const& directory);
auto                  InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void;
}
