#include <sdl-rdp/video/frame/capture.hpp>

#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <freerdp/freerdp.h>
#include <freerdp/update.h>

namespace sdl_rdp::video::frame::detail::capture {
using sdl_rdp::picture::ApplyDesktopSize;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::SameSize;

FrameCapture::FrameCapture(PeerLink& link, FrameStore& store, PeerFrames& frames, DesktopLayout& desktop,
                           FramePacing& pacing, FrameStatistics& statistics, Encoder const& encoder) noexcept
    : _link{ link }, _store{ store }, _frames{ frames }, _desktop{ desktop }, _pacing{ pacing },
      _statistics{ statistics }, _encoder{ encoder } { }
auto FrameCapture::Next() -> CaptureState {
  if (!_frames.Snapshot() && !Begin()) return CaptureState::Failed;
  return _frames.Snapshot() && !_desktop.Resizing() ? CaptureState::Captured : CaptureState::Idle;
}
auto FrameCapture::Begin() -> bool {
  Expects(!_desktop.Resizing(), "no resize in flight");
  auto const active = freerdp_is_active_state(&_link.Context());
  Expects(active, "a frame begins on an active client");
  auto const picture = Take();
  if (!_frames.Snapshot()) return true;
  if (!_desktop.Matches(picture) && !Resize(picture)) return false;
  _pacing.Begin();
  return true;
}
auto FrameCapture::Take() -> Rect {
  auto const frame = _store.Lock();
  if (!_frames.Dirty(frame) || !_store.Snapshot(frame)) return _desktop.Desktop();
  auto const picture = _store.Picture(frame);
  if (!_desktop.Matches(picture) || !SameSize(_frames.Snapshot().Bounds(), _store.Bounds(frame)))
    _pacing.Restart(frame);
  _statistics.Begin(_encoder.EncodeTime(), _frames.Capture(frame));
  return picture;
}
auto FrameCapture::Resize(Rect picture) -> bool {
  auto& context = _link.Context();
  _desktop.BeginResize(picture);
  if (!ApplyDesktopSize(*context.settings, picture) || !context.update->DesktopResize(&context)) return false;
  _frames.Resend();
  return true;
}
}
