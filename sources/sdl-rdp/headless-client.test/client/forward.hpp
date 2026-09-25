#pragma once

namespace sdl_rdp::headless_client_test::client::detail::sound {
class SoundClient;
}
namespace sdl_rdp::headless_client_test::client::detail::sound_protocol {
struct SoundProtocol;
}

namespace sdl_rdp::headless_client_test::client {
using detail::sound::SoundClient;
using detail::sound_protocol::SoundProtocol;
}
