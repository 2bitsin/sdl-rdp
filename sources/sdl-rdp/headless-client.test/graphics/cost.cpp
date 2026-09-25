#include <sdl-rdp/headless-client.test/graphics/cost.hpp>

#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>

#include <array>
#include <cstdint>

namespace sdl_rdp::headless_client_test::graphics::detail::cost {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::MovingTilePattern;
using sdl_rdp::utilities::Rect;

auto GraphicsCost::Open(std::uint32_t width, std::uint32_t height, Codec codec) -> void {
  OpenGraphics("cost", width, height, codec);
}
auto GraphicsCost::PresentMovingTiles(Client& client, std::size_t frames) -> void {
  Pixels     pixels(1920uz * 1080);
  Rect const full  { .x = 0, .y = 0, .w = 1920, .h = 1080 };
  for (std::size_t frame = 0; frame < frames; ++frame) {
    MovingTilePattern(pixels, 1920, 1080, frame);
    backend.Present(pixels, 1920, 1080, full);
    ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(client, backend, logs));
  }
}
auto GraphicsCost::PresentPlanar(Client& client, GraphicsObserver& observer, Pixels const& pixels,
                                 Pixels const& expected, Rect area) -> void {
  auto count = observer.Observed().frames.size();
  backend.Present(pixels, 354, 226, area);
  EXPECT_TRUE(client.Until([&] { return observer.Observed().frames.size() > count; }));
  EXPECT_EQ(client.MaxError(expected), 0u);
}
}
