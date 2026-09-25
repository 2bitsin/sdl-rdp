#pragma once

namespace sdl_rdp::auth::detail::authenticator {
class Authenticator;
}
namespace sdl_rdp::auth::detail::credentials {
class Credentials;
}

namespace sdl_rdp::auth {
using detail::authenticator::Authenticator;
using detail::credentials::Credentials;
}
