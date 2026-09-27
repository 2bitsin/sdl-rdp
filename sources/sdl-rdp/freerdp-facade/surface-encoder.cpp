#include <sdl-rdp/freerdp-facade/surface-encoder.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/settings_types.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/stream.h>
#include <algorithm>
#include <array>
#include <utility>
#include <variant>

namespace sdl_rdp::freerdp_facade::detail::surface_encoder {
using sdl_rdp::freerdp_facade::CodecSetupFailed;
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::Stride;
using sdl_rdp::utilities::Unreachable;

namespace {
constexpr std::size_t InitialStreamCapacity = 64uz * 1024;
// abi: Stream_Free takes the flag that frees the buffer with the stream.
auto FreeStream(wStream* stream) noexcept -> void {
  Stream_Free(stream, true);
}
using StreamHandle    = std::unique_ptr<wStream, Releases<FreeStream>>;
using RemoteFxContext = std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>;
using NscContext      = std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>;
constexpr std::array NscParameters{
  std::pair{ NSC_COLOR_FORMAT, std::uint32_t{ PIXEL_FORMAT_BGRX32 } },
  std::pair{ NSC_COLOR_LOSS_LEVEL, std::uint32_t{ 1 } },
  std::pair{ NSC_ALLOW_SUBSAMPLING, std::uint32_t{ 0 } },
};
class RemoteFx {
public:
  RemoteFx() : _context{ rfx_context_new_ex(true, THREADING_FLAGS_DISABLE_THREADS) } {
    if (!_context) throw AllocationFailed{ "RemoteFX context" };
    rfx_context_set_pixel_format(_context.get(), PIXEL_FORMAT_BGRX32);
  }
  auto Compose(wStream& stream, std::span<std::uint8_t const> bgrx, Extent size) -> bool {
    if (!Resize(size)) return false;
    RFX_RECT const rect{ 0, 0, Narrowed<std::uint16_t>(size.width), Narrowed<std::uint16_t>(size.height) };
    return rfx_compose_message(_context.get(), &stream, &rect, 1, bgrx.data(), size.width, size.height,
                               Stride(size.width));
  }

private:
  auto Resize(Extent size) -> bool {
    if (size == _size) return true;
    if (!rfx_context_reset(_context.get(), size.width, size.height)) return false;
    _size = size;
    return true;
  }
  RemoteFxContext _context;
  Extent          _size   { };
};
class NsCodec {
public:
  NsCodec() : _context{ nsc_context_new() } {
    if (!_context) throw AllocationFailed{ "NSCodec context" };
    auto const set = [&](auto parameter) {
      return nsc_context_set_parameters(_context.get(), parameter.first, parameter.second);
    };
    if (!std::ranges::all_of(NscParameters, set)) throw CodecSetupFailed{ "NSCodec" };
  }
  auto Compose(wStream& stream, std::span<std::uint8_t const> bgrx, Extent size) -> bool {
    return nsc_compose_message(_context.get(), &stream, bgrx.data(), size.width, size.height, Stride(size.width));
  }

private:
  NscContext _context;
};
using Codec = std::variant<RemoteFx, NsCodec>;
auto NewCodec(SurfaceCodec codec) -> Codec {
  switch (codec) {
  case SurfaceCodec::RemoteFx: return Codec{ std::in_place_type<RemoteFx> };
  case SurfaceCodec::NsCodec:  return Codec{ std::in_place_type<NsCodec> };
  default:                     Unreachable(codec);
  }
}
auto NewStream() -> StreamHandle {
  StreamHandle stream{ Stream_New(nullptr, InitialStreamCapacity) };
  if (!stream) throw AllocationFailed{ "Codec stream" };
  return stream;
}
}
struct SurfaceEncoder::State {
  Codec        codec;
  StreamHandle stream{ NewStream() };
};
SurfaceEncoder::SurfaceEncoder(SurfaceCodec codec) : _state{ std::make_unique<State>(NewCodec(codec)) } { }
SurfaceEncoder::~SurfaceEncoder() = default;
auto SurfaceEncoder::Encode(std::span<std::uint8_t const> bgrx, Extent size)
    -> std::optional<std::span<std::byte const>> {
  Expects(size.width > 0, "surface picture width is positive");
  Expects(size.height > 0, "surface picture height is positive");
  Expects(bgrx.size() == std::size_t{ size.width } * size.height * PixelBytes, "surface picture is packed");
  auto& stream = *_state->stream;
  Stream_ResetPosition(&stream);
  auto const composed = std::visit([&](auto& codec) { return codec.Compose(stream, bgrx, size); }, _state->codec);
  if (!composed) return std::nullopt;
  return oxbox::utilities::AsBytes(std::span{ Stream_ConstBuffer(&stream), Stream_GetPosition(&stream) });
}
}
