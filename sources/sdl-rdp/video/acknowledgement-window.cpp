#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <winpr/synch.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace Backend {
auto WaitMilliseconds(std::chrono::nanoseconds remaining, std::int64_t floor) -> std::uint32_t {
  return Narrowed<std::uint32_t>(std::max(floor, std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
}
auto AcknowledgementWindow::Enabled() const noexcept -> bool {
  return _enabled;
}
auto AcknowledgementWindow::Enable() noexcept -> void {
  _enabled = true;
}
auto AcknowledgementWindow::Disable() noexcept -> void {
  _enabled = false;
  Clear();
}
auto AcknowledgementWindow::Clear() noexcept -> void {
  _pending.clear();
}
auto AcknowledgementWindow::Next() noexcept -> std::uint32_t {
  return ++_frame_id;
}
auto AcknowledgementWindow::Frame() const noexcept -> std::uint32_t {
  return _frame_id;
}
auto AcknowledgementWindow::Record(std::uint64_t sequence, Clock::time_point now) -> void {
  if (_enabled) _pending.emplace_back(_frame_id, sequence, now);
}
auto AcknowledgementWindow::Accept(std::uint32_t id) -> std::vector<SentFrame> {
  auto found = std::ranges::find(_pending, id, &SentFrame::Id);
  if (found == _pending.end()) return { };
  _acknowledged = found->Presented();
  std::vector<SentFrame> settled(std::make_move_iterator(_pending.begin()), std::make_move_iterator(found + 1));
  _pending.erase(_pending.begin(), found + 1);
  return settled;
}
auto AcknowledgementWindow::Expire(Clock::time_point now) -> std::size_t {
  std::size_t expired = 0;
  for (; !_pending.empty() && _pending.front().Age(now) >= AcknowledgementTimeout; ++expired) {
    _acknowledged = _pending.front().Presented();
    _pending.pop_front();
  }
  return expired;
}
auto AcknowledgementWindow::Open(std::size_t window) const noexcept -> bool {
  return _pending.size() < window;
}
auto AcknowledgementWindow::Remaining(Clock::time_point now) const -> std::uint32_t {
  if (_pending.empty()) return INFINITE;
  return WaitMilliseconds(AcknowledgementTimeout - _pending.front().Age(now), 1);
}
auto AcknowledgementWindow::Settled(std::uint64_t target) const noexcept -> bool {
  return !_enabled || _acknowledged + 1 >= target;
}
auto AcknowledgementWindow::Acknowledged() const noexcept -> std::uint64_t {
  return _acknowledged;
}
}
