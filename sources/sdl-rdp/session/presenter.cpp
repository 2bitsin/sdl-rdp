#include <sdl-rdp/session/presenter.hpp>

#include <sdl-rdp/core/configuration.hpp>
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/core/frame-store.hpp>
#include <sdl-rdp/session/peer.hpp>
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/video/avc.hpp>
#include <sdl-rdp/video/pointer-store.hpp>

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
  auto const prior  = former ? former.Pixels() : std::span<BYTE const>{ };
  if (!prior.empty())
    Expects(prior.size() >= stride * (former.Height() - 1) + row, "previous frame covers every composed row");
  auto const prior_stride = prior.empty() ? std::size_t{ 0 } : stride;
  auto const prior_row    = prior.empty() ? std::size_t{ 0 } : row;
  std::ranges::for_each(std::views::iota(0u, former.Height()), [&](unsigned y) {
    auto active = damage | std::views::filter([y](auto rect) { return Spans(rect.y, rect.h, int(y)); });
    ComposeRow(source.subspan(std::size_t(y) * pitch, row), prior.subspan(std::size_t(y) * prior_stride, prior_row),
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
  auto const locked  = _session.LockPeersAndFrame();
  auto const resized = _frames.Publish(locked.Frame(), std::move(next), size);
  locked.ForEach([&](Peer& peer, FrameLock const& frame) {
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
  auto const             locked  = _session.LockPeersAndFrame();
  if (!_frames.Resize(locked.Frame(), size)) return;
  if (auto* const current = _session.Current(session)) current->RestartPacing(locked.Frame());
  locked.ForEach([&](Peer& peer, FrameLock const& frame) { peer.Repaint(frame, Whole(size)); });
}
auto Presenter::SetAspect(sdlrdp_aspect value) -> void {
  auto const locked = _session.LockPeersAndFrame();
  _frames.SetAspect(locked.Frame(), value);
  locked.ForEach([&](Peer& peer, FrameLock const& frame) { peer.Repaint(frame, _frames.Bounds(frame)); });
}
auto Presenter::SetRefresh(RefreshMode mode, unsigned ceiling) -> void {
  auto const locked = _session.LockPeersAndFrame();
  _configuration.SetRefresh(mode, ceiling);
  locked.ForEach([](Peer& peer, FrameLock const& frame) { peer.RestartPacing(frame); });
}
auto Presenter::SetCodec(sdlrdp_codec codec) -> void {
  _configuration.SetCodec(codec);
}
auto Presenter::SetPointer(PointerShape shape) -> void {
  auto const session = _session.Lock();
  _pointer.Replace(std::move(shape));
  _session.ForEachPeer([](Peer& peer) { peer.Signal(); });
}
auto Presenter::WaitFrame(Deadline deadline) -> int {
  auto       frame  = _frames.Lock();
  auto const target = _frames.Presented(frame);
  return _frames.WaitFor(frame, deadline, [&] {
    auto* const current = _session.Current(frame);
    return !current || current->Settled(frame, target);
  });
}
}
