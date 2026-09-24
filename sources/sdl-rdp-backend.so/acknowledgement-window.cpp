#include "_detail/acknowledgement-window.hpp"

#include <algorithm>
#include <iterator>
#include <winpr/synch.h>

namespace Backend {
DWORD WaitMilliseconds(std::chrono::nanoseconds remaining, int64_t floor) {
  return DWORD(std::max(floor, std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
}
bool AcknowledgementWindow::Enabled() const noexcept {
  return _enabled;
}
void AcknowledgementWindow::Enable() noexcept {
  _enabled = true;
}
void AcknowledgementWindow::Disable() noexcept {
  _enabled = false;
  Clear();
}
void AcknowledgementWindow::Clear() noexcept {
  _pending.clear();
}
UINT32 AcknowledgementWindow::Next() noexcept {
  return ++_frame_id;
}
UINT32 AcknowledgementWindow::Frame() const noexcept {
  return _frame_id;
}
void AcknowledgementWindow::Record(uint64_t sequence, Clock::time_point now) {
  if (_enabled) _pending.emplace_back(_frame_id, sequence, now);
}
std::vector<SentFrame> AcknowledgementWindow::Accept(UINT32 id) {
  auto found = std::ranges::find(_pending, id, &SentFrame::Id);
  if (found == _pending.end()) return { };
  _acknowledged = found->Sequence();
  std::vector<SentFrame> settled(std::make_move_iterator(_pending.begin()), std::make_move_iterator(found + 1));
  _pending.erase(_pending.begin(), found + 1);
  return settled;
}
unsigned AcknowledgementWindow::Expire(Clock::time_point now) {
  unsigned expired = 0;
  for (; !_pending.empty() && _pending.front().Age(now) >= AcknowledgementTimeout; ++expired) {
    _acknowledged = _pending.front().Sequence();
    _pending.pop_front();
  }
  return expired;
}
bool AcknowledgementWindow::Open(unsigned window) const noexcept {
  return _pending.size() < window;
}
DWORD AcknowledgementWindow::Remaining(Clock::time_point now) const {
  if (_pending.empty()) return INFINITE;
  return WaitMilliseconds(AcknowledgementTimeout - _pending.front().Age(now), 1);
}
bool AcknowledgementWindow::Settled(uint64_t target) const noexcept {
  return !_enabled || _acknowledged + 1 >= target;
}
uint64_t AcknowledgementWindow::Acknowledged() const noexcept {
  return _acknowledged;
}
}
