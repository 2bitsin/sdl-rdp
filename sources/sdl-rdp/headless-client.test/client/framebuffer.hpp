#pragma once
#include <freerdp/gdi/gdi.h>
#include <cstdint>
#include <span>

namespace sdl_rdp::headless_client_test::client::detail::framebuffer {
auto Framebuffer(rdpGdi const& gdi) -> std::span<std::uint32_t const>;
}

namespace sdl_rdp::headless_client_test::client {
using detail::framebuffer::Framebuffer;
}
