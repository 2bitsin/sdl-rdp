#include "_detail/peer-wait.hpp"

#include "_detail/acknowledgement-window.hpp"
#include "_detail/activation.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/graphics-link.hpp"
#include "_detail/peer-link.hpp"

#include <algorithm>
#include <freerdp/channels/wtsvc.h>

namespace Backend {
namespace {
// WinPR BIO signals readability only; retry blocked output every 5 ms for static frames.
constexpr DWORD BlockedRetry = 5;
}
PeerWait::PeerWait(PeerLink& link, ChannelSet const& channels, Activation const& activation, FramePacing& pacing,
                   GraphicsLink& graphics) noexcept
    : _link { link }, _channels{ channels }, _activation{ activation }, _pacing{ pacing }, _graphics{ graphics } { }
WaitPlan PeerWait::Plan(std::span<HANDLE> handles) {
  _graphics.ExpireConfirmation();
  if (!_activation.Activated()) _link.Invalidate();
  auto const count = _link.Handles([&] { return Collect(handles); });
  return { .count = count, .timeout = Timeout() };
}
DWORD PeerWait::Collect(std::span<HANDLE> handles) {
  Expects(handles.size() > AppendedHandleCount, "event array has room for transport and peer handles");
  auto&      client    = _link.Client();
  auto const budget    = handles.size() - AppendedHandleCount;
  auto const transport = client.GetEventHandles(&client, handles.data(), budget);
  if (!transport) return 0;
  Expects(transport <= budget, "transport respects event budget");
  auto const rest = _channels.Handles(handles.subspan(transport));
  Expects(rest.size() >= LoopHandleCount, "the loop's own handles fit");
  rest[0] = _link.Wake();
  rest[1] = WTSVirtualChannelManagerGetEventHandle(_link.Channels());
  return DWORD(handles.size() - rest.size() + LoopHandleCount);
}
DWORD PeerWait::Timeout() const {
  auto const blocked = _link.WriteBlocked();
  if (_activation.Holding()) {
    auto const remaining = _activation.ActivatedAt() + GraphicsConnectionWait - Activation::Clock::now();
    auto const wait      = WaitMilliseconds(remaining, 0);
    return blocked ? std::min(wait, BlockedRetry) : wait;
  }
  return blocked ? BlockedRetry : _pacing.Timeout();
}
}
