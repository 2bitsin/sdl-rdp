#include "_detail/acknowledgement-window.hpp"

#include <algorithm>
#include <iterator>
#include <winpr/synch.h>

namespace Backend {
auto WaitMilliseconds(std::chrono::nanoseconds remaining, int64_t floor) -> DWORD {
  return DWORD(std::max(floor, std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
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
auto AcknowledgementWindow::Next() noexcept -> UINT32 {
  return ++_frame_id;
}
auto AcknowledgementWindow::Frame() const noexcept -> UINT32 {
  return _frame_id;
}
auto AcknowledgementWindow::Record(uint64_t sequence, Clock::time_point now) -> void {
  if (_enabled) _pending.emplace_back(_frame_id, sequence, now);
}
auto AcknowledgementWindow::Accept(UINT32 id) -> std::vector<SentFrame> {
  auto found = std::ranges::find(_pending, id, &SentFrame::Id);
  if (found == _pending.end()) return { };
  _acknowledged = found->Sequence();
  std::vector<SentFrame> settled(std::make_move_iterator(_pending.begin()), std::make_move_iterator(found + 1));
  _pending.erase(_pending.begin(), found + 1);
  return settled;
}
auto AcknowledgementWindow::Expire(Clock::time_point now) -> unsigned {
  unsigned expired = 0;
  for (; !_pending.empty() && _pending.front().Age(now) >= AcknowledgementTimeout; ++expired) {
    _acknowledged = _pending.front().Sequence();
    _pending.pop_front();
  }
  return expired;
}
auto AcknowledgementWindow::Open(unsigned window) const noexcept -> bool {
  return _pending.size() < window;
}
auto AcknowledgementWindow::Remaining(Clock::time_point now) const -> DWORD {
  if (_pending.empty()) return INFINITE;
  return WaitMilliseconds(AcknowledgementTimeout - _pending.front().Age(now), 1);
}
auto AcknowledgementWindow::Settled(uint64_t target) const noexcept -> bool {
  return !_enabled || _acknowledged + 1 >= target;
}
auto AcknowledgementWindow::Acknowledged() const noexcept -> uint64_t {
  return _acknowledged;
}
}
