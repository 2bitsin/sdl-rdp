#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/storage/drive-channel.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/channels/rdpdr.h>
#include <winpr/nt.h>
#include <algorithm>
#include <format>
#include <utility>

namespace Backend {
auto DriveChannel::Disconnect() -> void {
  std::scoped_lock const lock(mutex);
  Shutdown();
  CloseTransport();
}
auto DriveChannel::Shutdown() -> void {
  if (!connected) return;
  connected = false;
  while (!devices.empty()) Remove(devices.begin()->second.wire);
  pending.clear();
  changed.notify_all();
  _link.Signal();
}
auto DriveChannel::CloseTransport() -> void {
  if (channel) _link.Invalidate();
  channel.reset();
  event = nullptr;
}
auto DriveChannel::Device(unsigned id) -> unsigned {
  if (!connected) throw std::runtime_error("Drive peer disconnected.");
  auto found = devices.find(id);
  if (found == devices.end()) throw std::runtime_error("Drive removed or peer disconnected.");
  return found->second.wire;
}
auto DriveChannel::List(sdlrdp_drive* out, unsigned max) -> int {
  std::scoped_lock const lock(mutex);
  unsigned               count = 0;
  for (auto const& [id, device] : devices) {
    if (count == max) break;
    out[count++] = device.drive;
  }
  return int(count);
}
auto DriveChannel::WaitAny(std::span<Slot const> slots) -> std::size_t {
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
auto DriveChannel::Wait(std::shared_ptr<DriveRequest> const& request, std::string const& path, bool end)
    -> DrivePacket {
  std::unique_lock lock(mutex);
  changed.wait(lock, [&] { return request->done || request->removed || !connected; });
  if (!connected) throw std::runtime_error("Drive peer disconnected: " + path);
  if (request->removed) throw std::runtime_error("Drive removed: " + path);
  if (request->status
      && (!end
          || (!std::cmp_equal(request->status, unsigned(STATUS_NO_MORE_FILES))
              && !std::cmp_equal(request->status, unsigned(STATUS_END_OF_FILE))))) {
    // WinPR owns the NTSTATUS name table; unknown client values retain their code.
    auto const* name   = NtStatus2Tag(static_cast<NTSTATUS>(request->status));
    auto        status = name ? std::format("{} (0x{:08x})", name, request->status)
                              : std::format("NTSTATUS 0x{:08x}", request->status);
    throw std::runtime_error(std::format("Drive '{}' failed: {}", path, status));
  }
  request->response.Origin(weak_from_this());
  return std::move(request->response);
}
} // namespace Backend
