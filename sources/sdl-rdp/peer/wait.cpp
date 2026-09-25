#include <sdl-rdp/peer/wait.hpp>

#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/graphics-link.hpp>

#include <freerdp/channels/wtsvc.h>
#include <algorithm>
#include <cstdint>

namespace sdl_rdp::peer::detail::wait {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::video::GraphicsConnectionWait;
using sdl_rdp::video::WaitMilliseconds;

namespace {
// WinPR BIO signals readability only; retry blocked output every 5 ms for static frames.
constexpr std::uint32_t BlockedRetry = 5;
}
PeerWait::PeerWait(PeerLink& link, ChannelSet const& channels, Activation const& activation, FramePacing& pacing,
                   GraphicsLink& graphics) noexcept
    : _link{ link }, _channels{ channels }, _activation{ activation }, _pacing{ pacing }, _graphics{ graphics } { }
auto PeerWait::Plan(std::span<WaitHandle> handles) -> WaitPlan {
  _graphics.ExpireConfirmation();
  if (!_activation.Activated()) _link.Invalidate();
  auto const count = _link.Handles([&] { return Collect(handles); });
  return { .count = count, .timeout = Timeout() };
}
auto PeerWait::Collect(std::span<WaitHandle> handles) -> std::uint32_t {
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
  return Narrowed<std::uint32_t>(handles.size() - rest.size() + LoopHandleCount);
}
auto PeerWait::Timeout() const -> std::uint32_t {
  auto const blocked = _link.WriteBlocked();
  if (_activation.Holding()) {
    auto const remaining = _activation.ActivatedAt() + GraphicsConnectionWait - Activation::Clock::now();
    auto const wait      = WaitMilliseconds(remaining, 0);
    return blocked ? std::min(wait, BlockedRetry) : wait;
  }
  return blocked ? BlockedRetry : _pacing.Timeout();
}
}
