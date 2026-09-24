#include "_detail/drive-channel.hpp"
#include "_detail/contract.hpp"
#include "_detail/peer-link.hpp"

#include <format>
#include <freerdp/channels/rdpdr.h>
#include <algorithm>
#include <utility>
#include <winpr/nt.h>

namespace Backend {
void DriveChannel::Disconnect() {
  std::scoped_lock const lock(mutex);
  Shutdown();
  CloseTransport();
}
void DriveChannel::Shutdown() {
  if (!connected) return;
  connected = false;
  while (!devices.empty())
    Remove(devices.begin()->second.wire);
  pending.clear();
  changed.notify_all();
  _link.Signal();
}
void DriveChannel::CloseTransport() {
  if (channel) _link.Invalidate();
  channel.reset();
  event = nullptr;
}
unsigned DriveChannel::Device(unsigned id) {

  if (!connected) throw std::runtime_error("Drive peer disconnected.");
  auto found = devices.find(id);
  if (found == devices.end()) throw std::runtime_error("Drive removed or peer disconnected.");
  return found->second.wire;
}
int DriveChannel::List(sdlrdp_drive* out, unsigned max) {
  std::scoped_lock const lock(mutex);
  unsigned               count = 0;
  for (auto const& [id, device] : devices) {
    if (count == max) break;
    out[count++] = device.drive;
  }
  return int(count);
}
size_t DriveChannel::WaitAny(std::span<Slot const> slots) {
  Expects(std::ranges::any_of(slots, [](auto const& slot) { return bool(slot.request); }),
          "transfer has outstanding requests");
  std::unique_lock lock(mutex);
  size_t           ready = slots.size();
  changed.wait(lock, [&] {
    auto const found = std::ranges::find_if(slots, [&](Slot const& slot) {
      return slot.request && (slot.request->done || slot.request->removed || !connected);
    });
    ready = size_t(found - slots.begin());
    return found != slots.end();
  });
  return ready;
}
DrivePacket DriveChannel::Wait(std::shared_ptr<DriveRequest> const& request, std::string const& path, bool end) {
  std::unique_lock lock(mutex);
  changed.wait(lock, [&] { return request->done || request->removed || !connected; });
  if (!connected) throw std::runtime_error("Drive peer disconnected: " + path);
  if (request->removed) throw std::runtime_error("Drive removed: " + path);
  if (request->status && (!end || (!std::cmp_equal(request->status, unsigned(STATUS_NO_MORE_FILES)) &&
                                   !std::cmp_equal(request->status, unsigned(STATUS_END_OF_FILE))))) {
    // WinPR owns the NTSTATUS name table; unknown client values retain their code.
    auto const* name   = NtStatus2Tag(static_cast<NTSTATUS>(request->status));
    auto        status =
        name ? std::format("{} (0x{:08x})", name, request->status) : std::format("NTSTATUS 0x{:08x}", request->status);
    throw std::runtime_error(std::format("Drive '{}' failed: {}", path, status));
  }
  request->response.Origin(weak_from_this());
  return std::move(request->response);
}
} // namespace Backend
