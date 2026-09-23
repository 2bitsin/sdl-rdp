#include "_detail/state.hpp"

#include <array>
#include <cstring>
#include <freerdp/channels/rdpdr.h>
#include <winpr/nt.h>

namespace Backend {
namespace {
DrivePacket Header(unsigned type) {
  DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2);
  packet.Put(type, 2);
  return packet;
}
HANDLE ChannelEvent(HANDLE channel) {
  void* data = nullptr;
  DWORD size = 0;
  if (!WTSVirtualChannelQuery(channel, WTSVirtualEventHandle, &data, &size))
    throw std::runtime_error("Drive channel event query failed.");
  auto* event = *static_cast<HANDLE*>(data);
  WTSFreeMemory(data);
  return event;
}
DrivePacket IoRequest(unsigned wire, unsigned file, unsigned id, unsigned major, unsigned minor,
                      DrivePacket const& body) {
  auto packet = Header(PAKID_CORE_DEVICE_IOREQUEST);
  packet.Put(wire);
  packet.Put(file);
  packet.Put(id);
  packet.Put(major);
  packet.Put(minor);
  packet.Append(body.Bytes());
  return packet;
}
void Capability(DrivePacket& packet, unsigned type, unsigned version, DrivePacket const& body) {
  constexpr unsigned header_size = (sizeof(uint16_t) * 2) + sizeof(uint32_t);
  Expects(body.Bytes().size() <= UINT16_MAX - header_size, "capability length fits its header");
  packet.Put(type, 2);
  packet.Put(header_size + body.Bytes().size(), 2);
  packet.Put(version);
  packet.Append(body.Bytes());
}
void GeneralCapability(DrivePacket& packet) {
  DrivePacket body;
  body.Put(0);
  body.Put(0);
  body.Put(RDPDR_VERSION_MAJOR, 2);
  body.Put(RDPDR_VERSION_MINOR_RDP6X, 2);
  constexpr unsigned all_irps = RDPDR_IRP_MJ_CREATE | RDPDR_IRP_MJ_CLEANUP | RDPDR_IRP_MJ_CLOSE | RDPDR_IRP_MJ_READ |
                                RDPDR_IRP_MJ_WRITE | RDPDR_IRP_MJ_FLUSH_BUFFERS | RDPDR_IRP_MJ_SHUTDOWN |
                                RDPDR_IRP_MJ_DEVICE_CONTROL | RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION |
                                RDPDR_IRP_MJ_SET_VOLUME_INFORMATION | RDPDR_IRP_MJ_QUERY_INFORMATION |
                                RDPDR_IRP_MJ_SET_INFORMATION | RDPDR_IRP_MJ_DIRECTORY_CONTROL |
                                RDPDR_IRP_MJ_LOCK_CONTROL | RDPDR_IRP_MJ_QUERY_SECURITY | RDPDR_IRP_MJ_SET_SECURITY;
  body.Put(all_irps);
  body.Put(0);
  body.Put(RDPDR_DEVICE_REMOVE_PDUS | RDPDR_CLIENT_DISPLAY_NAME_PDU | RDPDR_USER_LOGGEDON_PDU);
  body.Put(ENABLE_ASYNCIO);
  body.Put(0);
  body.Put(0);
  Capability(packet, CAP_GENERAL_TYPE, GENERAL_CAPABILITY_VERSION_02, body);
}
void DriveCapability(DrivePacket& packet) {
  Capability(packet, CAP_DRIVE_TYPE, DRIVE_CAPABILITY_VERSION_02, {});
}
} // namespace
void DriveChannel::Abort(std::string const& cause) {
  std::scoped_lock const lock(mutex);
  Fail(cause);
}
DriveChannel::DriveChannel(Peer& value) : peer(value) {}
DriveChannel::~DriveChannel() {
  Disconnect();
}
bool DriveChannel::Open() {
  Expects(!channel, "drive channel opens once");
  try {
    auto name = std::to_array(RDPDR_CHANNEL_NAME);
    channel   = WTSVirtualChannelOpen(peer.channels, WTS_CURRENT_SESSION, name.data());
    if (!channel) throw std::runtime_error("Drive channel open failed.");
    event       = ChannelEvent(channel);
    auto packet = Header(PAKID_CORE_SERVER_ANNOUNCE);
    packet.Put(RDPDR_VERSION_MAJOR, 2);
    packet.Put(RDPDR_VERSION_MINOR_RDP6X, 2);
    packet.Put(client_id);
    Write(packet);
    return true;
  } catch (std::exception const& error) {
    Fail(error.what());
    return false;
  }
}
void DriveChannel::Write(DrivePacket& packet) {
  Expects(channel != nullptr, "drive transport exists");
  ULONG written = 0;
  if (!WTSVirtualChannelWrite(channel, reinterpret_cast<char*>(packet.Bytes().data()), packet.Bytes().size(),
                              &written) ||
      written != packet.Bytes().size())
    throw std::runtime_error("Drive transport disconnected.");
  peer.wake.Transition(WakeEvent::Phase::Pending);
}
void DriveChannel::Capabilities() {
  auto               packet           = Header(PAKID_CORE_SERVER_CAPABILITY);
  constexpr unsigned capability_count = 2;
  packet.Put(capability_count, 2);
  packet.Put(0, 2);
  GeneralCapability(packet);
  DriveCapability(packet);
  Write(packet);
  packet = Header(PAKID_CORE_CLIENTID_CONFIRM);
  packet.Put(RDPDR_VERSION_MAJOR, 2);
  packet.Put(RDPDR_VERSION_MINOR_RDP6X, 2);
  packet.Put(client_id);
  Write(packet);
  auto logged_on = Header(PAKID_CORE_USER_LOGGEDON);
  Write(logged_on);
}
void DriveChannel::Announce(DrivePacket& packet) {
  auto count = packet.Get(4);
  while (count--) {
    auto type = packet.Get(4);
    auto wire = packet.Get(4);
    std::array<char, 9> name{};
    for (unsigned i = 0; i < 8; ++i)
      name[i] = char(packet.Get(1));
    auto length = packet.Get(4);
    auto begin  = packet.Position();
    packet.Skip(length);
    auto response = Header(PAKID_CORE_DEVICE_REPLY);
    response.Put(wire);
    response.Put(type == RDPDR_DTYP_FILESYSTEM ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED);
    Write(response);
    if (type != RDPDR_DTYP_FILESYSTEM) continue;
    auto label = Name(std::span(packet.Bytes()).subspan(begin, length), name.data());
    AnnounceDevice(unsigned(wire), label);
  }
}
void DriveChannel::ClientCapabilities(DrivePacket& packet) {
  constexpr unsigned capability_header_size = 8;
  auto               count                  = packet.Get(2);
  packet.Skip(2);
  while (count--) {
    auto start   = packet.Position();
    auto type    = packet.Get(2);
    auto length  = packet.Get(2);
    auto version = packet.Get(4);
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
void DriveChannel::Warn(std::string const& cause) const {
  if (connected) peer.owner.Log(SDLRDP_LOG_WARN, cause);
}
void DriveChannel::Fail(std::string const& cause) {
  if (!connected) return;
  Warn("Drive channel ended: " + cause);
  Shutdown();
}
void DriveChannel::Remove(unsigned wire) {
  for (auto it = devices.begin(); it != devices.end();) {
    if (it->second.wire != wire) {
      ++it;
      continue;
    }
    sdlrdp_event removed{ .type = SDLRDP_DRIVE, .drive = { .added = 0, .id = it->first, .name = {} } };
    std::strncpy(removed.drive.name, it->second.drive.name, sizeof(removed.drive.name) - 1);
    peer.owner.Push(removed);
    for (auto const& [id, request] : pending) {
      if (request->drive == it->first) request->removed = true;
    }
    changed.notify_all();
    it = devices.erase(it);
  }
}
void DriveChannel::Complete(DrivePacket& packet) {
  packet.Get(4);
  auto id     = packet.Get(4);
  auto status = packet.Get(4);
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
void DriveChannel::Receive(DrivePacket& packet) {
  if (packet.Get(2) != RDPDR_CTYP_CORE) return;
  auto type = packet.Get(2);
  if (type == PAKID_CORE_CLIENTID_CONFIRM) {
    packet.Skip(4);
    client_id = packet.Get(4);
  } else if (type == PAKID_CORE_CLIENT_CAPABILITY)
    ClientCapabilities(packet);
  else if (type == PAKID_CORE_CLIENT_NAME)
    Capabilities();
  else if (type == PAKID_CORE_DEVICELIST_ANNOUNCE)
    Announce(packet);
  else if (type == PAKID_CORE_DEVICE_IOCOMPLETION)
    Complete(packet);
  else if (type == PAKID_CORE_DEVICELIST_REMOVE) {
    auto count = packet.Get(4);
    while (count--)
      Remove(packet.Get(4));
  }
}
bool DriveChannel::Pump(std::span<HANDLE const> signaled) {
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
std::shared_ptr<DriveRequest> DriveChannel::Send(unsigned drive, unsigned file, unsigned major, DrivePacket const& body,
                                                 unsigned minor) {
  std::scoped_lock const lock(mutex);
  if (!connected) throw std::runtime_error("Drive channel ended.");
  auto wire = Device(drive);
  auto id   = next++;
  if (!id || pending.contains(id)) throw std::runtime_error("Drive completion ids exhausted.");
  auto request = std::make_shared<DriveRequest>();
  request->drive = drive;
  pending.emplace(id, request);
  auto packet = IoRequest(wire, file, id, major, minor, body);
  try {
    Write(packet);
  } catch (std::exception const& error) {
    Fail(error.what());
    throw;
  }
  return request;
}
void DriveChannel::AnnounceDevice(unsigned wire, std::string const& label) {
  auto id = peer.owner.next_drive.fetch_add(1);
  DeviceEntry entry{ .wire = wire, .drive = { id, {} } };
  std::strncpy(entry.drive.name, label.c_str(), sizeof(entry.drive.name) - 1);
  devices.emplace(id, entry);
  sdlrdp_event added{ .type = SDLRDP_DRIVE, .drive = { .added = 1, .id = id, .name = {} } };
  std::strncpy(added.drive.name, label.c_str(), sizeof(added.drive.name) - 1);
  peer.owner.Push(added);
}
void DriveChannel::GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length,
                                           unsigned version) const {
  constexpr unsigned general_caps_v1_size          = 40;
  constexpr unsigned protocol_major_version_offset = 16;
  constexpr unsigned io_code_fields_size           = 8;
  if (length < general_caps_v1_size) throw std::runtime_error("Truncated general drive capability.");
  packet.Seek(start + protocol_major_version_offset);
  auto major = packet.Get(2);
  auto minor = packet.Get(2);
  packet.Skip(io_code_fields_size);
  auto flags = packet.Get(4);
  peer.owner.Log(
      SDLRDP_LOG_INFO,
      std::format("Drive client version {}.{}, general capability {}, extended PDU 0x{:08x}, device removal {}.", major,
                  minor, version, flags, bool(flags & RDPDR_DEVICE_REMOVE_PDUS)));
}
bool DriveChannel::PumpAvailable() {
  for (;;) {
    if (!Signalled(event)) return true;
    ULONG length = 0;
    if (!WTSVirtualChannelRead(channel, 0, nullptr, 0, &length)) throw std::runtime_error("Drive channel read failed.");
    if (!length) return true;
    DrivePacket packet;
    packet.Bytes().resize(length);
    if (!WTSVirtualChannelRead(channel, 0, reinterpret_cast<char*>(packet.Bytes().data()), length, &length))
      throw std::runtime_error("Drive channel read failed.");
    packet.Bytes().resize(length);
    Receive(packet);
  }
}

}
