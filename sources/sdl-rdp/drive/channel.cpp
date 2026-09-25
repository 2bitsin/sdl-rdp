#include <sdl-rdp/drive/channel.hpp>
#include <freerdp/channels/rdpdr.h>
#include <oxbox/utilities/span.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/drive/capabilities.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/label.hpp>
#include <sdl-rdp/freerdp-facade/waitable.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>
#include <winpr/nt.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::drive::detail::channel {
using Backend::Contained;
using Backend::CopyTerminated;
using Backend::Diagnostics;
using Backend::EventQueue;
using Backend::Narrowed;
using Backend::PeerLink;
using Backend::SessionAccess;
using Backend::WaitHandle;
using utilities::Expects;
namespace {
auto Header(std::uint32_t type) -> DrivePacket {
  DrivePacket packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(Narrowed<std::uint16_t>(type));
  return packet;
}
auto ChannelEvent(WaitHandle channel) -> WaitHandle {
  void*         data = nullptr;
  std::uint32_t size = 0;
  if (!WTSVirtualChannelQuery(channel, WTSVirtualEventHandle, &data, &size)) throw DriveChannelFailed{ "event query" };
  auto* event = *static_cast<WaitHandle*>(data);
  WTSFreeMemory(data);
  return event;
}
auto Announcement(std::uint32_t type, std::uint32_t client_id) -> DrivePacket {
  auto packet = Header(type);
  packet.Write(std::uint16_t{ RDPDR_VERSION_MAJOR });
  packet.Write(std::uint16_t{ RDPDR_VERSION_MINOR_RDP6X });
  packet.Write(client_id);
  return packet;
}
auto IoRequest(std::span<std::uint32_t const> header, DrivePacket const& body) -> DrivePacket {
  auto packet = Header(PAKID_CORE_DEVICE_IOREQUEST);
  std::ranges::for_each(header, [&packet](std::uint32_t field) { packet.Write(field); });
  packet.Append(body.Bytes());
  return packet;
}
auto DriveEvent(bool added, sdlrdp_drive const& drive) -> sdlrdp_event {
  sdlrdp_event event{ .type = SDLRDP_DRIVE, .drive = { .added = added ? 1 : 0, .id = drive.id, .name = { } } };
  CopyTerminated(event.drive.name, drive.name);
  return event;
}
// Logs why the channel ends while its peer is connected; the caller shuts the channel down after it.
auto Ending(DriveChannel const& channel) -> auto {
  return [&channel](std::string_view cause) { channel.Warn(std::format("Drive channel ended: {}", cause)); };
}
} // namespace
auto DriveChannel::Abort(std::string const& cause) -> void {
  std::scoped_lock const lock(mutex);
  Fail(cause);
}
DriveChannel::DriveChannel(PeerLink& link, EventQueue& events, Diagnostics const& diagnostics,
                           SessionAccess& session) noexcept
    : _link{ link }, _events{ events }, _diagnostics{ diagnostics }, _session{ session } { }
DriveChannel::~DriveChannel() {
  Disconnect();
}
auto DriveChannel::Event() const -> WaitHandle {
  return event;
}
auto DriveChannel::Open() -> bool {
  Expects(!channel, "drive channel opens once");
  auto const opened = [this] {
    auto name = std::to_array(RDPDR_CHANNEL_NAME);
    channel.reset(WTSVirtualChannelOpen(_link.Channels(), WTS_CURRENT_SESSION, name.data()));
    if (!channel) throw DriveChannelFailed{ "open" };
    event = ChannelEvent(channel.get());
    auto packet = Announcement(PAKID_CORE_SERVER_ANNOUNCE, client_id);
    Write(packet);
    return true;
  };
  if (Contained(false, opened, Ending(*this))) return true;
  Shutdown();
  return false;
}
auto DriveChannel::Write(DrivePacket& packet) -> void {
  Expects(channel != nullptr, "drive transport exists");
  std::uint32_t written = 0;
  if (!WTSVirtualChannelWrite(channel.get(), oxbox::utilities::SpanCast<char>(std::span(packet.Bytes())).data(),
                              packet.Bytes().size(), &written)
      || written != packet.Bytes().size())
    throw TransportDisconnected{ };
  _link.Signal();
}
auto DriveChannel::Capabilities() -> void {
  auto                    packet           = Header(PAKID_CORE_SERVER_CAPABILITY);
  constexpr std::uint32_t capability_count = 2;
  packet.Write(std::uint16_t{ capability_count });
  packet.Write(std::uint16_t{ 0 });
  GeneralCapability(packet);
  DriveCapability(packet);
  Write(packet);
  packet = Announcement(PAKID_CORE_CLIENTID_CONFIRM, client_id);
  Write(packet);
  auto logged_on = Header(PAKID_CORE_USER_LOGGEDON);
  Write(logged_on);
}
auto DriveChannel::Announce(DrivePacket& packet) -> void {
  auto count = packet.Read<std::uint32_t>();
  while (count--) {
    auto                type = packet.Read<std::uint32_t>();
    auto                wire = packet.Read<std::uint32_t>();
    std::array<char, 9> name { };
    for (std::size_t i = 0; i < 8; ++i) name[i] = static_cast<char>(packet.Read<std::uint8_t>());
    auto length = packet.Read<std::uint32_t>();
    auto begin  = packet.Position();
    packet.Skip(length);
    auto response = Header(PAKID_CORE_DEVICE_REPLY);
    response.Write(wire);
    response.Write(std::bit_cast<std::uint32_t>(type == RDPDR_DTYP_FILESYSTEM ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED));
    Write(response);
    if (type != RDPDR_DTYP_FILESYSTEM) continue;
    auto label = Name(std::span(packet.Bytes()).subspan(begin, length), name.data());
    AnnounceDevice(wire, label);
  }
}
auto DriveChannel::ClientCapabilities(DrivePacket& packet) -> void {
  constexpr std::size_t capability_header_size = 8;
  auto                  count                  = packet.Read<std::uint16_t>();
  packet.Skip(2);
  while (count--) {
    auto start   = packet.Position();
    auto type    = packet.Read<std::uint16_t>();
    auto length  = packet.Read<std::uint16_t>();
    auto version = packet.Read<std::uint32_t>();
    if (length < capability_header_size) throw ShortCapability{ type, length };
    packet.Skip(length - capability_header_size);
    auto end = packet.Position();
    if (type == CAP_DRIVE_TYPE) drive_version = version;
    if (type == CAP_GENERAL_TYPE) {
      GeneralClientCapability(packet, start, length, version);
    }
    packet.Seek(end);
  }
}
auto DriveChannel::Warn(std::string_view cause) const -> void {
  if (connected) _diagnostics.Log(SDLRDP_LOG_WARN, std::string{ cause });
}
auto DriveChannel::Fail(std::string_view cause) -> void {
  if (!connected) return;
  std::invoke(Ending(*this), cause);
  Shutdown();
}
auto DriveChannel::Remove(std::uint32_t wire) -> void {
  for (auto it = devices.begin(); it != devices.end();) {
    if (it->second.wire != wire) {
      ++it;
      continue;
    }
    _events.Push(DriveEvent(false, it->second.drive));
    for (auto const& request : pending | std::views::values | std::views::filter([&](auto const& candidate) {
                                 return candidate->drive == it->first;
                               }))
      request->removed = true;
    changed.notify_all();
    it = devices.erase(it);
  }
}
auto DriveChannel::Complete(DrivePacket& packet) -> void {
  packet.Read<std::uint32_t>();
  auto id     = packet.Read<std::uint32_t>();
  auto status = packet.Read<std::uint32_t>();
  auto found  = pending.find(id);
  if (found == pending.end()) {
    Warn(std::format("Unknown drive completion id {}; ignored.", id));
    return;
  }
  auto& request = *found->second;
  request.status = status;
  request.response.Bytes().assign(packet.Bytes().begin() + Narrowed<std::ptrdiff_t>(packet.Position()),
                                  packet.Bytes().end());
  request.done = true;
  pending.erase(found);
  changed.notify_all();
}
auto DriveChannel::Receive(DrivePacket& packet) -> void {
  if (packet.Read<std::uint16_t>() != RDPDR_CTYP_CORE) return;
  auto type = packet.Read<std::uint16_t>();
  if (type == PAKID_CORE_CLIENTID_CONFIRM) {
    packet.Skip(4);
    client_id = packet.Read<std::uint32_t>();
  } else if (type == PAKID_CORE_CLIENT_CAPABILITY)
    ClientCapabilities(packet);
  else if (type == PAKID_CORE_CLIENT_NAME)
    Capabilities();
  else if (type == PAKID_CORE_DEVICELIST_ANNOUNCE)
    Announce(packet);
  else if (type == PAKID_CORE_DEVICE_IOCOMPLETION)
    Complete(packet);
  else if (type == PAKID_CORE_DEVICELIST_REMOVE) {
    auto count = packet.Read<std::uint32_t>();
    while (count--) Remove(packet.Read<std::uint32_t>());
  }
}
auto DriveChannel::Pump(std::span<WaitHandle const> signaled) -> bool {
  std::scoped_lock const lock(mutex);
  if (!connected) {
    CloseTransport();
    return true;
  }
  if (!std::ranges::contains(signaled, event)) return true;
  auto const pumped = Contained(
      std::optional<bool>{ }, [this] -> std::optional<bool> { return PumpAvailable(); }, Ending(*this));
  if (pumped) return *pumped;
  Shutdown();
  return true;
}
auto DriveChannel::Send(std::uint32_t drive, std::uint32_t file, freerdp_facade::IrpMajor major,
                        DrivePacket const& body, freerdp_facade::IrpMinor minor) -> std::shared_ptr<DriveRequest> {
  std::scoped_lock const lock(mutex);
  if (!connected) throw PeerDisconnected{ "request" };
  auto wire = Device(drive);
  auto id   = next++;
  if (!id || pending.contains(id)) throw CompletionIdsExhausted{ };
  auto request = std::make_shared<DriveRequest>();
  request->drive = drive;
  pending.emplace(id, request);
  auto packet = IoRequest(std::array{ wire, file, id, std::to_underlying(major), std::to_underlying(minor) }, body);
  try {
    Write(packet);
  } catch (std::exception const& error) {
    Fail(error.what());
    throw;
  }
  return request;
}
auto DriveChannel::AnnounceDevice(std::uint32_t wire, std::string const& label) -> void {
  auto        id    = _session.NextDrive();
  DeviceEntry entry { .wire = wire, .drive = { id, { } } };
  CopyTerminated(entry.drive.name, label);
  devices.emplace(id, entry);
  _events.Push(DriveEvent(true, entry.drive));
}
auto DriveChannel::GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length,
                                           std::uint32_t version) const -> void {
  constexpr std::size_t general_caps_v1_size          = 40;
  constexpr std::size_t protocol_major_version_offset = 16;
  constexpr std::size_t io_code_fields_size           = 8;
  if (length < general_caps_v1_size) throw ShortCapability{ CAP_GENERAL_TYPE, length };
  packet.Seek(start + protocol_major_version_offset);
  auto major = packet.Read<std::uint16_t>();
  auto minor = packet.Read<std::uint16_t>();
  packet.Skip(io_code_fields_size);
  auto flags = packet.Read<std::uint32_t>();
  _diagnostics.Log(
      SDLRDP_LOG_INFO,
      std::format("Drive client version {}.{}, general capability {}, extended PDU 0x{:08x}, device removal {}.", major,
                  minor, version, flags, (flags & RDPDR_DEVICE_REMOVE_PDUS) != 0));
}
auto DriveChannel::PumpAvailable() -> bool {
  sdl_rdp::freerdp_facade::Waitable const ready{ event };
  for (;;) {
    if (!ready.Signalled()) return true;
    std::uint32_t length = 0;
    if (!WTSVirtualChannelRead(channel.get(), 0, nullptr, 0, &length)) throw DriveChannelFailed{ "read" };
    if (!length) return true;
    DrivePacket packet;
    packet.Bytes().resize(length);
    if (!WTSVirtualChannelRead(channel.get(), 0, oxbox::utilities::SpanCast<char>(std::span(packet.Bytes())).data(),
                               length, &length))
      throw DriveChannelFailed{ "read" };
    packet.Bytes().resize(length);
    Receive(packet);
  }
}
auto DriveChannel::Name(std::span<std::byte const> bytes, std::string_view dos) const -> std::string {
  auto                  label    = Contained(
      std::string{ dos }, [&] { return DecodeLabel(bytes, drive_version, dos); },
      [&](std::string_view cause) { Warn(std::format("{} Using DOS name '{}'.", cause, dos)); });
  constexpr std::size_t capacity = sizeof(sdlrdp_drive::name) - 1;
  if (label.size() > capacity) {
    Warn(std::format("Drive name exceeds {} bytes; truncating.", capacity));
    auto end = capacity;
    while ((std::bit_cast<std::uint8_t>(label[end]) & 0xc0) == 0x80) --end;
    label.resize(end);
  }
  return label;
}
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
auto DriveChannel::Device(std::uint32_t id) -> std::uint32_t {
  Expects(connected, "a request addresses a connected peer");
  auto found = devices.find(id);
  if (found == devices.end()) throw DriveRemoved{ "the requested drive" };
  return found->second.wire;
}
auto DriveChannel::List(std::span<sdlrdp_drive> out) -> int {
  std::scoped_lock const lock(mutex);
  auto const             listed = devices | std::views::values | std::views::transform(&DeviceEntry::drive)
                                  | std::views::take(out.size());
  return Narrowed<int>(std::ranges::copy(listed, out.begin()).out - out.begin());
}
auto DriveChannel::WaitAny(std::span<Slot const> slots) -> std::size_t {
  Expects(std::ranges::any_of(slots, [](auto const& slot) { return slot.request != nullptr; }),
          "transfer has outstanding requests");
  std::unique_lock lock(mutex);
  std::size_t      ready = slots.size();
  changed.wait(lock, [&] {
    auto const found = std::ranges::find_if(slots, [&](Slot const& slot) {
      return slot.request && (slot.request->done || slot.request->removed || !connected);
    });
    ready = Narrowed<std::size_t>(found - slots.begin());
    return found != slots.end();
  });
  return ready;
}
auto DriveChannel::Wait(std::shared_ptr<DriveRequest> const& request, std::string const& path, bool end)
    -> DrivePacket {
  std::unique_lock lock(mutex);
  changed.wait(lock, [&] { return request->done || request->removed || !connected; });
  if (!connected) throw PeerDisconnected{ path };
  if (request->removed) throw DriveRemoved{ path };
  if (request->status
      && (!end
          || (request->status != std::bit_cast<std::uint32_t>(STATUS_NO_MORE_FILES)
              && request->status != std::bit_cast<std::uint32_t>(STATUS_END_OF_FILE)))) {
    // WinPR owns the NTSTATUS name table; unknown client values retain their code.
    auto const* name = NtStatus2Tag(static_cast<NTSTATUS>(request->status));
    throw StatusFailure{ path, name ? name : "unknown NTSTATUS", request->status };
  }
  request->response.Origin(weak_from_this());
  return std::move(request->response);
}
}
