#include <sdl-rdp/storage/drive-channel.hpp>
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/core/event-queue.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/core/session-access.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <freerdp/channels/rdpdr.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/nt.h>
#include <algorithm>
#include <array>
#include <bit>
#include <ranges>
#include <span>

namespace Backend {
namespace {
auto Header(unsigned type) -> DrivePacket {
  DrivePacket packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(Narrowed<std::uint16_t>(type));
  return packet;
}
auto ChannelEvent(HANDLE channel) -> HANDLE {
  void* data = nullptr;
  DWORD size = 0;
  if (!WTSVirtualChannelQuery(channel, WTSVirtualEventHandle, &data, &size))
    throw std::runtime_error("Drive channel event query failed.");
  auto* event = *static_cast<HANDLE*>(data);
  WTSFreeMemory(data);
  return event;
}
auto Announcement(unsigned type, unsigned client_id) -> DrivePacket {
  auto packet = Header(type);
  packet.Write(std::uint16_t{ RDPDR_VERSION_MAJOR });
  packet.Write(std::uint16_t{ RDPDR_VERSION_MINOR_RDP6X });
  packet.Write(std::uint32_t{ client_id });
  return packet;
}
auto IoRequest(std::span<unsigned const> header, DrivePacket const& body) -> DrivePacket {
  auto packet = Header(PAKID_CORE_DEVICE_IOREQUEST);
  std::ranges::for_each(header, [&packet](std::uint32_t field) { packet.Write(field); });
  packet.Append(body.Bytes());
  return packet;
}
auto Capability(DrivePacket& packet, unsigned type, unsigned version, DrivePacket const& body) -> void {
  constexpr unsigned header_size = (sizeof(uint16_t) * 2) + sizeof(uint32_t);
  Expects(body.Bytes().size() <= UINT16_MAX - header_size, "capability length fits its header");
  packet.Write(Narrowed<std::uint16_t>(type));
  packet.Write(Narrowed<std::uint16_t>(header_size + body.Bytes().size()));
  packet.Write(std::uint32_t{ version });
  packet.Append(body.Bytes());
}
auto GeneralCapability(DrivePacket& packet) -> void {
  DrivePacket body;
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint16_t{ RDPDR_VERSION_MAJOR });
  body.Write(std::uint16_t{ RDPDR_VERSION_MINOR_RDP6X });
  constexpr std::uint32_t all_irps = RDPDR_IRP_MJ_CREATE | RDPDR_IRP_MJ_CLEANUP | RDPDR_IRP_MJ_CLOSE | RDPDR_IRP_MJ_READ
                                     | RDPDR_IRP_MJ_WRITE | RDPDR_IRP_MJ_FLUSH_BUFFERS | RDPDR_IRP_MJ_SHUTDOWN
                                     | RDPDR_IRP_MJ_DEVICE_CONTROL | RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION
                                     | RDPDR_IRP_MJ_SET_VOLUME_INFORMATION | RDPDR_IRP_MJ_QUERY_INFORMATION
                                     | RDPDR_IRP_MJ_SET_INFORMATION | RDPDR_IRP_MJ_DIRECTORY_CONTROL
                                     | RDPDR_IRP_MJ_LOCK_CONTROL | RDPDR_IRP_MJ_QUERY_SECURITY
                                     | RDPDR_IRP_MJ_SET_SECURITY;
  body.Write(all_irps);
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ RDPDR_DEVICE_REMOVE_PDUS | RDPDR_CLIENT_DISPLAY_NAME_PDU | RDPDR_USER_LOGGEDON_PDU });
  body.Write(std::uint32_t{ ENABLE_ASYNCIO });
  body.Write(std::uint32_t{ 0 });
  body.Write(std::uint32_t{ 0 });
  Capability(packet, CAP_GENERAL_TYPE, GENERAL_CAPABILITY_VERSION_02, body);
}
auto DriveEvent(bool added, sdlrdp_drive const& drive) -> sdlrdp_event {
  sdlrdp_event event{ .type = SDLRDP_DRIVE, .drive = { .added = added ? 1 : 0, .id = drive.id, .name = { } } };
  CopyTerminated(event.drive.name, drive.name);
  return event;
}
auto DriveCapability(DrivePacket& packet) -> void {
  Capability(packet, CAP_DRIVE_TYPE, DRIVE_CAPABILITY_VERSION_02, { });
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
auto DriveChannel::Event() const -> HANDLE {
  return event;
}
auto DriveChannel::Open() -> bool {
  Expects(!channel, "drive channel opens once");
  try {
    auto name = std::to_array(RDPDR_CHANNEL_NAME);
    channel.reset(WTSVirtualChannelOpen(_link.Channels(), WTS_CURRENT_SESSION, name.data()));
    if (!channel) throw std::runtime_error("Drive channel open failed.");
    event = ChannelEvent(channel.get());
    auto packet = Announcement(PAKID_CORE_SERVER_ANNOUNCE, client_id);
    Write(packet);
    return true;
  } catch (std::exception const& error) {
    Fail(error.what());
    return false;
  }
}
auto DriveChannel::Write(DrivePacket& packet) -> void {
  Expects(channel != nullptr, "drive transport exists");
  ULONG written = 0;
  if (!WTSVirtualChannelWrite(channel.get(), oxbox::utilities::SpanCast<char>(std::span(packet.Bytes())).data(),
                              packet.Bytes().size(), &written)
      || written != packet.Bytes().size())
    throw std::runtime_error("Drive transport disconnected.");
  _link.Signal();
}
auto DriveChannel::Capabilities() -> void {
  auto               packet           = Header(PAKID_CORE_SERVER_CAPABILITY);
  constexpr unsigned capability_count = 2;
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
  auto count = packet.Read<uint32_t>();
  while (count--) {
    auto                type = packet.Read<uint32_t>();
    auto                wire = packet.Read<uint32_t>();
    std::array<char, 9> name { };
    for (std::size_t i = 0; i < 8; ++i) name[i] = char(packet.Read<uint8_t>());
    auto length = packet.Read<uint32_t>();
    auto begin  = packet.Position();
    packet.Skip(length);
    auto response = Header(PAKID_CORE_DEVICE_REPLY);
    response.Write(std::uint32_t{ wire });
    response.Write(std::bit_cast<std::uint32_t>(type == RDPDR_DTYP_FILESYSTEM ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED));
    Write(response);
    if (type != RDPDR_DTYP_FILESYSTEM) continue;
    auto label = Name(std::span(packet.Bytes()).subspan(begin, length), name.data());
    AnnounceDevice(unsigned(wire), label);
  }
}
auto DriveChannel::ClientCapabilities(DrivePacket& packet) -> void {
  constexpr unsigned capability_header_size = 8;
  auto               count                  = packet.Read<uint16_t>();
  packet.Skip(2);
  while (count--) {
    auto start   = packet.Position();
    auto type    = packet.Read<uint16_t>();
    auto length  = packet.Read<uint16_t>();
    auto version = packet.Read<uint32_t>();
    if (length < capability_header_size) throw std::runtime_error("Invalid drive capability length.");
    packet.Skip(length - capability_header_size);
    auto end = packet.Position();
    if (type == CAP_DRIVE_TYPE) drive_version = version;
    if (type == CAP_GENERAL_TYPE) {
      GeneralClientCapability(packet, start, length, unsigned(version));
    }
    packet.Seek(end);
  }
}
auto DriveChannel::Warn(std::string const& cause) const -> void {
  if (connected) _diagnostics.Log(SDLRDP_LOG_WARN, cause);
}
auto DriveChannel::Fail(std::string const& cause) -> void {
  if (!connected) return;
  Warn("Drive channel ended: " + cause);
  Shutdown();
}
auto DriveChannel::Remove(unsigned wire) -> void {
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
  packet.Read<uint32_t>();
  auto id     = packet.Read<uint32_t>();
  auto status = packet.Read<uint32_t>();
  auto found  = pending.find(id);
  if (found == pending.end()) {
    Warn(std::format("Unknown drive completion id {}; ignored.", id));
    return;
  }
  auto& request = *found->second;
  request.status = status;
  request.response.Bytes().assign(packet.Bytes().begin() + std::ptrdiff_t(packet.Position()), packet.Bytes().end());
  request.done = true;
  pending.erase(found);
  changed.notify_all();
}
auto DriveChannel::Receive(DrivePacket& packet) -> void {
  if (packet.Read<uint16_t>() != RDPDR_CTYP_CORE) return;
  auto type = packet.Read<uint16_t>();
  if (type == PAKID_CORE_CLIENTID_CONFIRM) {
    packet.Skip(4);
    client_id = packet.Read<uint32_t>();
  } else if (type == PAKID_CORE_CLIENT_CAPABILITY)
    ClientCapabilities(packet);
  else if (type == PAKID_CORE_CLIENT_NAME)
    Capabilities();
  else if (type == PAKID_CORE_DEVICELIST_ANNOUNCE)
    Announce(packet);
  else if (type == PAKID_CORE_DEVICE_IOCOMPLETION)
    Complete(packet);
  else if (type == PAKID_CORE_DEVICELIST_REMOVE) {
    auto count = packet.Read<uint32_t>();
    while (count--) Remove(packet.Read<uint32_t>());
  }
}
auto DriveChannel::Pump(std::span<HANDLE const> signaled) -> bool {
  std::scoped_lock const lock(mutex);
  if (!connected) {
    CloseTransport();
    return true;
  }
  if (!std::ranges::contains(signaled, event)) return true;
  try {
    return PumpAvailable();
  } catch (std::exception const& error) {
    Fail(error.what());
    return true;
  }
}
auto DriveChannel::Send(unsigned drive, unsigned file, unsigned major, DrivePacket const& body, unsigned minor)
    -> std::shared_ptr<DriveRequest> {
  std::scoped_lock const lock(mutex);
  if (!connected) throw std::runtime_error("Drive channel ended.");
  auto wire = Device(drive);
  auto id   = next++;
  if (!id || pending.contains(id)) throw std::runtime_error("Drive completion ids exhausted.");
  auto request = std::make_shared<DriveRequest>();
  request->drive = drive;
  pending.emplace(id, request);
  auto packet = IoRequest(std::array{ wire, file, id, major, minor }, body);
  try {
    Write(packet);
  } catch (std::exception const& error) {
    Fail(error.what());
    throw;
  }
  return request;
}
auto DriveChannel::AnnounceDevice(unsigned wire, std::string const& label) -> void {
  auto        id    = _session.NextDrive();
  DeviceEntry entry { .wire = wire, .drive = { id, { } } };
  CopyTerminated(entry.drive.name, label);
  devices.emplace(id, entry);
  _events.Push(DriveEvent(true, entry.drive));
}
auto DriveChannel::GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length,
                                           unsigned version) const -> void {
  constexpr unsigned general_caps_v1_size          = 40;
  constexpr unsigned protocol_major_version_offset = 16;
  constexpr unsigned io_code_fields_size           = 8;
  if (length < general_caps_v1_size) throw std::runtime_error("Truncated general drive capability.");
  packet.Seek(start + protocol_major_version_offset);
  auto major = packet.Read<uint16_t>();
  auto minor = packet.Read<uint16_t>();
  packet.Skip(io_code_fields_size);
  auto flags = packet.Read<uint32_t>();
  _diagnostics.Log(
      SDLRDP_LOG_INFO,
      std::format("Drive client version {}.{}, general capability {}, extended PDU 0x{:08x}, device removal {}.", major,
                  minor, version, flags, bool(flags & RDPDR_DEVICE_REMOVE_PDUS)));
}
auto DriveChannel::PumpAvailable() -> bool {
  for (;;) {
    if (!Signalled(event)) return true;
    ULONG length = 0;
    if (!WTSVirtualChannelRead(channel.get(), 0, nullptr, 0, &length))
      throw std::runtime_error("Drive channel read failed.");
    if (!length) return true;
    DrivePacket packet;
    packet.Bytes().resize(length);
    if (!WTSVirtualChannelRead(channel.get(), 0, oxbox::utilities::SpanCast<char>(std::span(packet.Bytes())).data(),
                               length, &length))
      throw std::runtime_error("Drive channel read failed.");
    packet.Bytes().resize(length);
    Receive(packet);
  }
}

}
