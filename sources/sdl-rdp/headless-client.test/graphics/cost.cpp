#include <sdl-rdp/headless-client.test/graphics/cost.hpp>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>

#include <sdl-rdp/headless-client.test/frame/pattern.hpp>

#include <array>
#include <cstdint>

namespace Headless {
auto GraphicsCost::Open(std::uint32_t width, std::uint32_t height, sdlrdp_codec codec) -> void {
  auto pattern = std::to_array("/tmp/sdlrdp-cost-XXXXXX");
  OpenGraphics(pattern.data(), width, height, codec);
}
auto GraphicsCost::PresentMovingTiles(Client& client, std::size_t frames) -> void {
  std::vector<std::uint32_t> pixels(1920uz * 1080);
  sdlrdp_rect const          full  { 0, 0, 1920, 1080 };
  for (std::size_t frame = 0; frame < frames; ++frame) {
    MovingTilePattern(pixels, 1920, 1080, frame);
    ASSERT_EQ(backend.Present(pixels, 1920, 1080, full), 0);
    ASSERT_NO_FATAL_FAILURE(sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged(client, backend, logs));
  }
}
auto GraphicsCost::PresentPlanar(Client& client, GraphicsObserver& observer, std::vector<std::uint32_t> const& pixels,
                                 std::vector<std::uint32_t> const& expected, sdlrdp_rect area) -> void {
  auto count = observer.Observed().frames.size();
  EXPECT_EQ(backend.Present(pixels, 354, 226, area), 0);
  EXPECT_TRUE(client.Until([&] { return observer.Observed().frames.size() > count; }));
  EXPECT_EQ(client.MaxError(expected, &expected), 0u);
}
}
