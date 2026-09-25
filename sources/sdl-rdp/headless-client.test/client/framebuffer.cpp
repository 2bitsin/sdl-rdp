#include <sdl-rdp/headless-client.test/client/framebuffer.hpp>

#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/span.hpp>
#include <cstddef>

namespace sdl_rdp::headless_client_test::client::detail::framebuffer {
using oxbox::utilities::SpanCast;
using sdl_rdp::utilities::Narrowed;

auto Framebuffer(rdpGdi const& gdi) -> std::span<std::uint32_t const> {
  auto const bytes = std::span(gdi.primary_buffer,
                               Narrowed<std::size_t>(gdi.stride) * Narrowed<std::size_t>(gdi.height));
  return SpanCast<std::uint32_t const>(bytes);
}
}
