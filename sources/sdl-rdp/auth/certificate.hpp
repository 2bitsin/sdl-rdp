#pragma once
#include <sdl-rdp/auth/credentials.hpp>

namespace sdl_rdp::auth::detail::certificate {
auto EnsureCertificate(Credentials const& credentials) -> void;
}

namespace sdl_rdp::auth {
using detail::certificate::EnsureCertificate;
}
