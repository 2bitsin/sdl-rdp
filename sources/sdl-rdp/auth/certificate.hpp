#pragma once
#include <sdl-rdp/auth/credentials.hpp>

#include <freerdp/settings.h>

namespace sdl_rdp::auth::detail::certificate {
auto EnsureCertificate(Credentials const& credentials)                               -> void;
auto InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void;
}

namespace sdl_rdp::auth {
using detail::certificate::EnsureCertificate;
using detail::certificate::InstallServerCredentials;
}
