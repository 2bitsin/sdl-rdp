#pragma once
#include "credentials.hpp"

#include <freerdp/settings.h>
#include <filesystem>

namespace Backend {
auto DefaultCertificateDirectory()                                                   -> std::filesystem::path;
auto EnsureCertificate(std::filesystem::path const& directory)                       -> Credentials;
auto InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void;
}
