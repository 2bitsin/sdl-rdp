#include "_detail/presenter.hpp"

#include "_detail/avc.hpp"
#include "_detail/configuration.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/frame-store.hpp"
#include "_detail/peer.hpp"
#include "_detail/pointer-store.hpp"
#include "_detail/session.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace Backend {
namespace {
auto Spans(int start, int length, int value) -> bool {
  return start <= value && value < start + length;
}
auto ComposeRow(std::span<BYTE const> source, std::span<BYTE const> former, std::span<BYTE> target, auto damage)
    -> void {
  auto const width = int(target.size() / PixelBytes);
  for (int x = 0; x < width;) {
    auto covered{ std::ranges::find_if(damage, [x](auto rect) { return Spans(rect.x, rect.w, x); })               };
    auto ahead  { damage | std::views::filter([x](auto rect) { return rect.x > x; })                              };
    auto nearest{ std::ranges::min_element(ahead, { }, &sdlrdp_rect::x)                                           };
    auto end    { covered != damage.end() ? covered->x + covered->w : nearest != ahead.end() ? nearest->x : width };
    auto output { target.subspan(x * PixelBytes, (end - x) * PixelBytes)                                          };
    auto input  { covered != damage.end() ? source : former                                                       };
    if (input.empty())
      std::ranges::fill(output, 0);
    else
      std::ranges::copy(input.subspan(x * PixelBytes, output.size()), output.begin());
    x = end;
  }
}
auto ComposePicture(std::span<BYTE const> source, unsigned pitch, FrameSnapshot const& former, std::span<BYTE> target,
                    std::span<sdlrdp_rect const> damage) -> void {
  auto const width  = former.Width();
  auto const stride = former.Stride();
  auto const row    = std::size_t(width) * PixelBytes;
  std::ranges::for_each(std::views::iota(0u, former.Height()), [&](unsigned y) {
    auto active = damage | std::views::filter([y](auto rect) { return Spans(rect.y, rect.h, int(y)); });
    ComposeRow(source.subspan(std::size_t(y) * pitch, row),
               former ? former.Pixels().subspan(std::size_t(y) * stride, row) : std::span<BYTE const>{ },
               target.subspan(std::size_t(y) * stride, row), active);
  });
}
}
Presenter::Presenter(Diagnostics const& diagnostics, FrameStore& frames, Session& session, PointerStore& pointer,
                     Configuration& configuration)
    : _diagnostics{ diagnostics }, _frames{ frames }, _session{ session }, _pointer{ pointer },
      _configuration{ configuration } { }
auto Presenter::Present(std::span<BYTE const> pixels, unsigned pitch, Extent size, std::span<sdlrdp_rect const> damage)
    -> void {
  Expects(pitch >= size.width * PixelBytes, "source pitch covers framebuffer rows");
  Expects(pixels.size() >= std::size_t(pitch) * size.height, "source framebuffer covers every row");
  _diagnostics.Line("present", [&] { return std::format("dirty={}", damage.size()); });
  if (damage.empty()) return;
  std::scoped_lock const lock(_producer);
  auto                   next     = Acquire(size);
  auto const             previous = _frames.Read(
      [size](FrameStore const& frames, FrameLock const& held) { return frames.Previous(held, size); });
  ComposePicture(pixels, pitch, previous, *next, damage);
  Avc::ReplicateEdges(*next, size);
  Publish(std::move(next), size, damage);
}
auto Presenter::Publish(std::shared_ptr<std::vector<BYTE> const> next, Extent size, std::span<sdlrdp_rect const> damage)
    -> void {
  auto const held    = _session.LockPeers();
  auto const frame   = _frames.Lock();
  auto const resized = _frames.Publish(frame, std::move(next), size);
  _session.ForEach(held, [&](Peer& peer) {
    if (resized) {
      peer.Present(frame, { });
      peer.Repaint(frame, Whole(size));
    } else
      peer.Present(frame, damage);
  });
}
auto Presenter::Acquire(Extent size) -> std::shared_ptr<std::vector<BYTE>> {
  auto unused = std::ranges::find_if(_pool, [](auto const& buffer) { return buffer.use_count() == 1; });
  if (unused == _pool.end()) unused = _pool.insert(_pool.end(), std::make_shared<std::vector<BYTE>>());
  (*unused)->resize(FrameBytes(size));
  return *unused;
}
auto Presenter::EnsurePicture() -> void {
  auto const session = _session.Lock();
  auto const frame   = _frames.Lock();
  if (!_frames.Ensure(frame)) return;
  if (auto* const current = _session.Current(session)) current->Repaint(frame, _frames.Bounds(frame));
}
auto Presenter::Resize(Extent size) -> void {
  Expects(size.width > 0, "picture width is positive");
  Expects(size.height > 0, "picture height is positive");
  std::scoped_lock const lock(_producer);
  auto const             session = _session.Lock();
  auto const             held    = _session.LockPeers();
  auto const             frame   = _frames.Lock();
  if (!_frames.Resize(frame, size)) return;
  if (auto* const current = _session.Current(session)) current->RestartPacing(frame);
  _session.ForEach(held, [&](Peer& peer) { peer.Repaint(frame, Whole(size)); });
}
auto Presenter::SetAspect(sdlrdp_aspect value) -> void {
  auto const held  = _session.LockPeers();
  auto const frame = _frames.Lock();
  _frames.SetAspect(frame, value);
  _session.ForEach(held, [&](Peer& peer) { peer.Repaint(frame, _frames.Bounds(frame)); });
}
auto Presenter::SetRefresh(RefreshMode mode, unsigned ceiling) -> void {
  auto const held  = _session.LockPeers();
  auto const frame = _frames.Lock();
  _configuration.SetRefresh(mode, ceiling);
  _session.ForEach(held, [&](Peer& peer) { peer.RestartPacing(frame); });
}
auto Presenter::SetCodec(sdlrdp_codec codec) -> void {
  _configuration.SetCodec(codec);
}
auto Presenter::SetPointer(PointerShape shape) -> void {
  auto const session = _session.Lock();
  auto const held    = _session.LockPeers();
  _pointer.Replace(std::move(shape));
  _session.ForEach(held, [](Peer& peer) { peer.Signal(); });
}
auto Presenter::WaitFrame(int timeout) -> int {
  auto       frame  = _frames.Lock();
  auto const target = _frames.Presented(frame);
  return _frames.WaitFor(frame, timeout, [&] {
    auto* const current = _session.Current(frame);
    return !current || current->Settled(frame, target);
  });
}
}
