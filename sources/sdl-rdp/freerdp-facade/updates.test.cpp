#include <sdl-rdp/freerdp-facade/updates.hpp>

#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/pointer.h>
#include <freerdp/update.h>
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::updates {
namespace {
using sdl_rdp::utilities::ConnectedSockets;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::SocketPair;

struct Recorded {
  SURFACE_FRAME_MARKER              marker          { };
  SURFACE_BITS_COMMAND              surface         { };
  std::vector<BITMAP_DATA>          bitmaps;
  std::optional<bool>               skip_compression;
  int                               resizes         { };
  POINTER_NEW_UPDATE                color           { };
  std::vector<POINTER_LARGE_UPDATE> large;
  std::optional<std::uint32_t>      system;
};
auto Of(rdpContext const& context) -> Recorded& {
  return *static_cast<Recorded*>(context.peer->ContextExtra);
}
constexpr OperationName Recording { "Recording" };
constexpr auto          Ignored   = [](Recorded const&, OperationName) { return [](std::string_view) { }; };
constexpr auto          Marker    = [](Recorded& recorded, SURFACE_FRAME_MARKER const& marker) {
  recorded.marker = marker;
  return true;
};
constexpr auto          Surface   = [](Recorded& recorded, SURFACE_BITS_COMMAND const& command) {
  recorded.surface = command;
  return true;
};
constexpr auto          Bitmaps   = [](Recorded& recorded, BITMAP_UPDATE const& batch) {
  recorded.bitmaps.assign(batch.rectangles, batch.rectangles + batch.number);
  recorded.skip_compression = batch.skipCompression != 0;
  return true;
};
constexpr auto          Resize    = [](Recorded& recorded) { return ++recorded.resizes == 1; };
constexpr auto          Color     = [](Recorded& recorded, POINTER_NEW_UPDATE const& color) {
  recorded.color = color;
  return true;
};
constexpr auto          Large     = [](Recorded& recorded, POINTER_LARGE_UPDATE const& large) {
  recorded.large.push_back(large);
  return false;
};
constexpr auto          System    = [](Recorded& recorded, POINTER_SYSTEM_UPDATE const& system) {
  recorded.system = system.type;
  return true;
};
auto Record(rdpUpdate& update) -> void {
  update.SurfaceFrameMarker = Handled<Of, Marker, Recording, Ignored, false>;
  update.SurfaceBits        = Handled<Of, Surface, Recording, Ignored, false>;
  update.BitmapUpdate       = Handled<Of, Bitmaps, Recording, Ignored, false>;
  update.DesktopResize      = Handled<Of, Resize, Recording, Ignored, false>;
}
auto Record(rdpPointerUpdate& pointer) -> void {
  pointer.PointerNew    = Handled<Of, Color, Recording, Ignored, false>;
  pointer.PointerLarge  = Handled<Of, Large, Recording, Ignored, false>;
  pointer.PointerSystem = Handled<Of, System, Recording, Ignored, false>;
}
class UpdateSlots : public testing::Test {
protected:
  UpdateSlots() {
    context.peer->ContextExtra = &recorded;
    Record(*context.update);
    Record(*context.update->pointer);
  }
  auto Sent(Bitmap const& bitmap) -> BITMAP_DATA {
    EXPECT_TRUE(updates.Bitmaps(std::span{ &bitmap, 1 }));
    EXPECT_EQ(recorded.bitmaps.size(), 1);
    EXPECT_EQ(recorded.skip_compression, true);
    return recorded.bitmaps.empty() ? BITMAP_DATA{ } : recorded.bitmaps.front();
  }
  Recorded                          recorded;
  SocketPair                        sockets   { ConnectedSockets()        };
  Connection                        connection{ std::move(sockets.server) };
  rdp_context&                      context   { connection.Context()      };
  Updates                           updates   { connection                };
  std::array<std::uint8_t, 8> const pixels    { 1, 2, 3, 4, 5, 6, 7, 8    };
  std::array<std::uint8_t, 4> const mask      { 0x80, 0, 0x40, 0          };
  PointerImage const                image     {
    .size = { .width = 2, .height = 1 }, .hot_x = 1, .hot_y = 2, .pixels = pixels, .mask = mask
  };
};
}
TEST_F(UpdateSlots, FrameMarkerCarriesActionAndFrame) {
  EXPECT_TRUE(updates.FrameMarker(FrameAction::End, 7));
  EXPECT_EQ(recorded.marker.frameAction, SURFACECMD_FRAMEACTION_END);
  EXPECT_EQ(recorded.marker.frameId, 7);
}
TEST_F(UpdateSlots, SurfaceBitsCornersAreExclusive) {
  std::array<std::byte, 5> const payload{ };
  EXPECT_TRUE(updates.SurfaceBits({ .area = { .x = 2, .y = 3, .w = 4, .h = 5 }, .codec_id = 9, .payload = payload }));
  auto const& command = recorded.surface;
  EXPECT_EQ(command.cmdType, CMDTYPE_SET_SURFACE_BITS);
  EXPECT_TRUE(command.skipCompression);
  EXPECT_EQ(command.destLeft, 2);
  EXPECT_EQ(command.destTop, 3);
  EXPECT_EQ(command.destRight, 6);
  EXPECT_EQ(command.destBottom, 8);
  EXPECT_EQ(command.bmp.bpp, 32);
  EXPECT_EQ(command.bmp.codecID, 9);
  EXPECT_EQ(command.bmp.width, 4);
  EXPECT_EQ(command.bmp.height, 5);
  EXPECT_EQ(command.bmp.bitmapDataLength, payload.size());
  EXPECT_EQ(static_cast<void const*>(command.bmp.bitmapData), static_cast<void const*>(payload.data()));
}
TEST_F(UpdateSlots, BitmapCornersAreInclusive) {
  std::array<std::byte, 48> const payload { };
  auto const                      full    = Sent({ .area = { .x = 1, .y = 2, .w = 3, .h = 4 }, .payload = payload });
  EXPECT_EQ(full.destLeft, 1);
  EXPECT_EQ(full.destTop, 2);
  EXPECT_EQ(full.destRight, 3);
  EXPECT_EQ(full.destBottom, 5);
  EXPECT_EQ(full.width, 3);
  EXPECT_EQ(full.height, 4);
  EXPECT_EQ(full.bitsPerPixel, 32);
  EXPECT_EQ(full.cbScanWidth, 12);
  EXPECT_EQ(full.cbUncompressedSize, 48);
  EXPECT_EQ(full.bitmapLength, 48);
  EXPECT_EQ(full.cbCompMainBodySize, 48);
  EXPECT_EQ(static_cast<void const*>(full.bitmapDataStream), static_cast<void const*>(payload.data()));
  EXPECT_FALSE(full.compressed);
}
TEST_F(UpdateSlots, SixteenBitRowsArePadded) {
  std::array<std::byte, 16> const payload{ };
  auto const packed = Sent({ .area = { .w = 3, .h = 2 }, .payload = payload, .depth = 16, .compressed = true });
  EXPECT_EQ(packed.width, 4);
  EXPECT_EQ(packed.bitsPerPixel, 16);
  EXPECT_EQ(packed.cbScanWidth, 8);
  EXPECT_EQ(packed.cbUncompressedSize, 16);
  EXPECT_EQ(packed.bitmapLength, 16);
  EXPECT_EQ(packed.cbCompMainBodySize, 16);
  EXPECT_TRUE(packed.compressed);
}
TEST_F(UpdateSlots, TwentyFourBitRowsArePadded) {
  std::array<std::byte, 24> const payload { };
  auto const                      packed  = Sent({ .area = { .w = 3, .h = 2 }, .payload = payload, .depth = 24 });
  EXPECT_EQ(packed.width, 4);
  EXPECT_EQ(packed.bitsPerPixel, 24);
  EXPECT_EQ(packed.cbScanWidth, 12);
  EXPECT_EQ(packed.cbUncompressedSize, 24);
  EXPECT_EQ(packed.bitmapLength, 24);
  EXPECT_FALSE(packed.compressed);
}
TEST_F(UpdateSlots, BitmapDepthIsAContract) {
  std::array<std::byte, 4> const payload{ };
  EXPECT_DEATH(std::ignore = Sent({ .area = { .w = 1, .h = 1 }, .payload = payload, .depth = 15 }),
               "supported bitmap depth");
}
TEST_F(UpdateSlots, DesktopResizeIsTheSlotsAnswer) {
  EXPECT_TRUE(updates.DesktopResize());
  EXPECT_FALSE(updates.DesktopResize());
}
TEST_F(UpdateSlots, PointerSendsTheCallersBuffers) {
  EXPECT_TRUE(updates.Pointer(image));
  auto const& color = recorded.color.colorPtrAttr;
  EXPECT_EQ(recorded.color.xorBpp, 32);
  EXPECT_EQ(color.cacheIndex, 0);
  EXPECT_EQ(color.hotSpotX, 1);
  EXPECT_EQ(color.hotSpotY, 2);
  EXPECT_EQ(color.width, 2);
  EXPECT_EQ(color.height, 1);
  EXPECT_EQ(color.lengthXorMask, pixels.size());
  EXPECT_EQ(color.lengthAndMask, mask.size());
  EXPECT_EQ(static_cast<void const*>(color.xorMaskData), static_cast<void const*>(pixels.data()));
  EXPECT_EQ(static_cast<void const*>(color.andMaskData), static_cast<void const*>(mask.data()));
}
TEST_F(UpdateSlots, LargePointerSendsTheCallersBuffers) {
  EXPECT_FALSE(updates.LargePointer(image));
  ASSERT_EQ(recorded.large.size(), 1);
  auto const& large = recorded.large[0];
  EXPECT_EQ(large.xorBpp, 32);
  EXPECT_EQ(large.cacheIndex, 0);
  EXPECT_EQ(large.hotSpotX, 1);
  EXPECT_EQ(large.hotSpotY, 2);
  EXPECT_EQ(large.width, 2);
  EXPECT_EQ(large.height, 1);
  EXPECT_EQ(large.lengthXorMask, pixels.size());
  EXPECT_EQ(large.lengthAndMask, mask.size());
  EXPECT_EQ(static_cast<void const*>(large.xorMaskData), static_cast<void const*>(pixels.data()));
  EXPECT_EQ(static_cast<void const*>(large.andMaskData), static_cast<void const*>(mask.data()));
}
TEST_F(UpdateSlots, HidePointerIsTheNullSystemPointer) {
  EXPECT_TRUE(updates.HidePointer());
  EXPECT_EQ(recorded.system, SYSPTR_NULL);
}
TEST(ConvertPixels, PacksEachRowToFourBytes) {
  std::array<std::uint8_t, 24> const bgrx   { 0x10, 0x20, 0x30, 0, 0x40, 0x50, 0x60, 0, 0x70, 0x80, 0x90, 0,
                                              0xa0, 0xb0, 0xc0, 0, 0xd0, 0xe0, 0xf0, 0, 0x01, 0x02, 0x03, 0 };
  auto const                         packed = ConvertPixels(bgrx, { .width = 3, .height = 2 }, PixelFormat::Bgr24);
  ASSERT_EQ(packed.size(), 24);
  EXPECT_EQ(packed[0], std::byte{ 0x10 });
  EXPECT_EQ(packed[8], std::byte{ 0x90 });
  EXPECT_EQ(packed[12], std::byte{ 0xa0 });
  EXPECT_EQ(ConvertPixels(bgrx, { .width = 3, .height = 2 }, PixelFormat::Rgb16).size(), 16);
}
}
