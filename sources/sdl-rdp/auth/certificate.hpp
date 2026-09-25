#pragma once
#include <sdl-rdp/auth/credentials.hpp>

#include <freerdp/settings.h>

namespace Backend {
auto EnsureCertificate(Credentials const& credentials)                               -> void;
auto InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void;
}
