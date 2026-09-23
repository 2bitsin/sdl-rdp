#include "_detail/state.hpp"
#include <freerdp/channels/rdpdr.h>
#include <winpr/nt.h>
#include <cstring>

namespace Backend {
namespace {
DrivePacket Header(unsigned type) {
  DrivePacket packet;
  packet.Put(RDPDR_CTYP_CORE, 2);
  packet.Put(type, 2);
  return packet;
}
void Capability(DrivePacket& packet, unsigned type, unsigned version, DrivePacket const& body) {
  constexpr unsigned header_size = sizeof(uint16_t) * 2 + sizeof(uint32_t);
  Expects(body.bytes.size() <= UINT16_MAX - header_size, "capability length fits its header");
  packet.Put(type, 2); packet.Put(header_size + body.bytes.size(), 2);
  packet.Put(version); packet.Append(body.bytes);
}
void GeneralCapability(DrivePacket& packet) {
  DrivePacket body;
  body.Put(0); body.Put(0);
  body.Put(RDPDR_VERSION_MAJOR, 2); body.Put(RDPDR_VERSION_MINOR_RDP6X, 2);
  constexpr unsigned all_irps = RDPDR_IRP_MJ_CREATE | RDPDR_IRP_MJ_CLEANUP | RDPDR_IRP_MJ_CLOSE
    | RDPDR_IRP_MJ_READ | RDPDR_IRP_MJ_WRITE | RDPDR_IRP_MJ_FLUSH_BUFFERS | RDPDR_IRP_MJ_SHUTDOWN
    | RDPDR_IRP_MJ_DEVICE_CONTROL | RDPDR_IRP_MJ_QUERY_VOLUME_INFORMATION | RDPDR_IRP_MJ_SET_VOLUME_INFORMATION
    | RDPDR_IRP_MJ_QUERY_INFORMATION | RDPDR_IRP_MJ_SET_INFORMATION | RDPDR_IRP_MJ_DIRECTORY_CONTROL
    | RDPDR_IRP_MJ_LOCK_CONTROL | RDPDR_IRP_MJ_QUERY_SECURITY | RDPDR_IRP_MJ_SET_SECURITY;
  body.Put(all_irps); body.Put(0);
  body.Put(RDPDR_DEVICE_REMOVE_PDUS | RDPDR_CLIENT_DISPLAY_NAME_PDU | RDPDR_USER_LOGGEDON_PDU);
  body.Put(ENABLE_ASYNCIO); body.Put(0); body.Put(0);
  Capability(packet, CAP_GENERAL_TYPE, GENERAL_CAPABILITY_VERSION_02, body);
}
void DriveCapability(DrivePacket& packet) {
  Capability(packet, CAP_DRIVE_TYPE, DRIVE_CAPABILITY_VERSION_02, {});
}
}
void DriveChannel::Abort(std::string const& cause) {
  std::scoped_lock lock(mutex);
  Fail(cause);
}
DriveChannel::DriveChannel(Peer& value) : peer(value) {}
DriveChannel::~DriveChannel() { Disconnect(); }
bool DriveChannel::Open()
{
  Expects(!channel, "drive channel opens once");
  try {
    channel = WTSVirtualChannelOpen(peer.channels, WTS_CURRENT_SESSION, const_cast<char*>(RDPDR_CHANNEL_NAME));
    if (!channel) throw std::runtime_error("Drive channel open failed.");
    void* data = nullptr;
    DWORD size = 0;
    if (!WTSVirtualChannelQuery(channel, WTSVirtualEventHandle, &data, &size))
      throw std::runtime_error("Drive channel event query failed.");
    event = *static_cast<HANDLE*>(data);
    WTSFreeMemory(data);
    auto packet = Header(PAKID_CORE_SERVER_ANNOUNCE);
    packet.Put(RDPDR_VERSION_MAJOR, 2); packet.Put(RDPDR_VERSION_MINOR_RDP6X, 2); packet.Put(client_id);
    Write(packet);
    return true;
  } catch (std::exception const& error) { Fail(error.what()); return false; }
}
void DriveChannel::Write(DrivePacket const& packet)
{
  Expects(channel != nullptr, "drive transport exists");
  ULONG written = 0;
  if (!WTSVirtualChannelWrite(channel, reinterpret_cast<char*>(const_cast<uint8_t*>(packet.bytes.data())),
      packet.bytes.size(), &written) || written != packet.bytes.size()) throw std::runtime_error("Drive transport disconnected.");
  peer.wake.Transition(WakeEvent::Phase::Pending);
}
void DriveChannel::Capabilities()
{
  auto packet = Header(PAKID_CORE_SERVER_CAPABILITY);
  constexpr unsigned capability_count = 2;
  packet.Put(capability_count, 2); packet.Put(0, 2);
  GeneralCapability(packet);
  DriveCapability(packet);
  Write(packet);
  packet = Header(PAKID_CORE_CLIENTID_CONFIRM);
  packet.Put(RDPDR_VERSION_MAJOR, 2); packet.Put(RDPDR_VERSION_MINOR_RDP6X, 2); packet.Put(client_id);
  Write(packet);
  Write(Header(PAKID_CORE_USER_LOGGEDON));
}
void DriveChannel::Announce(DrivePacket& packet)
{
  auto count = packet.Get(4);
  while (count--) {
    auto type = packet.Get(4), wire = packet.Get(4);
    char name[9]{};
    for (unsigned i = 0; i < 8; ++i) name[i] = char(packet.Get(1));
    auto length = packet.Get(4);
    auto begin = packet.position;
    packet.Skip(length);
    auto response = Header(PAKID_CORE_DEVICE_REPLY);
    response.Put(wire); response.Put(type == RDPDR_DTYP_FILESYSTEM ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED);
    Write(response);
    if (type != RDPDR_DTYP_FILESYSTEM) continue;
    auto label = Name(std::span(packet.bytes).subspan(begin, length), name);
    auto id = peer.owner.next_drive.fetch_add(1);
    DeviceEntry entry{unsigned(wire), {id, {}}};
    std::strncpy(entry.drive.name, label.c_str(), sizeof(entry.drive.name) - 1);
    devices.emplace(id, entry);
    sdlrdp_event added{.type = SDLRDP_DRIVE, .drive = {1, id, {}}};
    std::strncpy(added.drive.name, label.c_str(), sizeof(added.drive.name) - 1);
    peer.owner.Push(added);
  }
}
std::string DriveChannel::Name(std::span<uint8_t const> bytes, char const* dos) {
  std::string label(dos);
  try {
    if (!bytes.empty()) {
      if (bytes.back()) throw std::runtime_error("Unterminated drive name.");
      // FreeRDP 3.15 sends UTF-8 despite advertising drive capability v2.
      bool wide = drive_version >= DRIVE_CAPABILITY_VERSION_02 && bytes.size() >= 2
        && bytes.size() % 2 == 0 && bytes[bytes.size() - 2] == 0;
      auto format = wide ? oxbox::utilities::TextFormat{oxbox::utilities::Encoding::UTF16, std::endian::little}
                         : oxbox::utilities::TextFormat{};
      label = TranscodeRange<std::string>(std::as_bytes(bytes.first(bytes.size() - (wide ? 2 : 1))), format, {});
      if (label.find('\0') != std::string::npos) throw std::runtime_error("Embedded null in drive name.");
    }
  } catch (std::exception const& error) { label = dos; Warn(std::format("{} Using DOS name '{}'.", error.what(), dos)); }
  constexpr size_t capacity = sizeof(sdlrdp_drive::name) - 1;
  if (label.size() > capacity) {
    Warn(std::format("Drive name exceeds {} bytes; truncating.", capacity));
    auto end = capacity;
    while ((uint8_t(label[end]) & 0xc0) == 0x80) --end;
    label.resize(end);
  }
  return label;
}
void DriveChannel::ClientCapabilities(DrivePacket& packet) {
  constexpr unsigned capability_header_size = 8, general_caps_v1_size = 40;
  constexpr unsigned protocol_major_version_offset = 16, io_code_fields_size = 8;
  auto count = packet.Get(2);
  packet.Skip(2);
  while (count--) {
    auto start = packet.position;
    auto type = packet.Get(2), length = packet.Get(2), version = packet.Get(4);
    if (length < capability_header_size) throw std::runtime_error("Invalid drive capability length.");
    packet.Skip(length - capability_header_size);
    auto end = packet.position;
    if (type == CAP_DRIVE_TYPE) drive_version = version;
    if (type == CAP_GENERAL_TYPE) {
      if (length < general_caps_v1_size) throw std::runtime_error("Truncated general drive capability.");
      packet.position = start + protocol_major_version_offset;
      auto major = packet.Get(2), minor = packet.Get(2);
      packet.Skip(io_code_fields_size);
      auto flags = packet.Get(4);
      peer.owner.Log(SDLRDP_LOG_INFO, std::format("Drive client version {}.{}, general capability {}, extended PDU 0x{:08x}, device removal {}.",
        major, minor, version, flags, bool(flags & RDPDR_DEVICE_REMOVE_PDUS)));
    }
    packet.position = end;
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
void DriveChannel::Remove(unsigned wire)
{
  for (auto it = devices.begin(); it != devices.end();) {
    if (it->second.wire != wire) { ++it; continue; }
    sdlrdp_event removed{.type = SDLRDP_DRIVE, .drive = {0, it->first, {}}};
    std::strncpy(removed.drive.name, it->second.drive.name, sizeof(removed.drive.name) - 1);
    peer.owner.Push(removed);
    for (auto const& [id, request] : pending) {
      if (request->drive == it->first) request->removed = true;
    }
    changed.notify_all();
    it = devices.erase(it);
  }
}
void DriveChannel::Complete(DrivePacket& packet)
{
  packet.Get(4);
  auto id = packet.Get(4), status = packet.Get(4);
  auto found = pending.find(id);
  if (found == pending.end()) { Warn(std::format("Unknown drive completion id {}; ignored.", id)); return; }
  auto& request = *found->second;
  request.status = status;
  request.response.bytes.assign(packet.bytes.begin() + packet.position, packet.bytes.end());
  request.done = true;
  pending.erase(found);
  changed.notify_all();
}
void DriveChannel::Receive(DrivePacket& packet)
{
  if (packet.Get(2) != RDPDR_CTYP_CORE) return;
  switch (packet.Get(2)) {
  case PAKID_CORE_CLIENTID_CONFIRM:
    packet.Skip(4); client_id = packet.Get(4); break;
  case PAKID_CORE_CLIENT_CAPABILITY: ClientCapabilities(packet); break;
  case PAKID_CORE_CLIENT_NAME: Capabilities(); break;
  case PAKID_CORE_DEVICELIST_ANNOUNCE: Announce(packet); break;
  case PAKID_CORE_DEVICE_IOCOMPLETION: Complete(packet); break;
  case PAKID_CORE_DEVICELIST_REMOVE: {
    auto count = packet.Get(4);
    while (count--) Remove(packet.Get(4));
    break;
  }
  default: break;
  }
}
bool DriveChannel::Pump(HANDLE signaled)
{
  std::scoped_lock lock(mutex);
  if (!connected) { CloseTransport(); return true; }
  if (signaled != event) return true;
  try {
    for (;;) {
      auto ready = WaitForSingleObject(event, 0);
      if (ready == WAIT_FAILED) throw std::runtime_error("Drive channel event wait failed.");
      if (ready != WAIT_OBJECT_0) return true;
      ULONG length = 0;
      if (!WTSVirtualChannelRead(channel, 0, nullptr, 0, &length))
        throw std::runtime_error("Drive channel read failed.");
      if (!length) return true;
      DrivePacket packet;
      packet.bytes.resize(length);
      if (!WTSVirtualChannelRead(channel, 0, reinterpret_cast<char*>(packet.bytes.data()), length, &length))
        throw std::runtime_error("Drive channel read failed.");
      packet.bytes.resize(length);
      Receive(packet);
    }
  } catch (std::exception const& error) { Fail(error.what()); return true; }
}
void DriveChannel::Disconnect()
{
  std::scoped_lock lock(mutex);
  Shutdown();
  CloseTransport();
}
void DriveChannel::Shutdown()
{
  if (!connected) return;
  connected = false;
  while (!devices.empty()) Remove(devices.begin()->second.wire);
  pending.clear();
  changed.notify_all();
  peer.wake.Transition(WakeEvent::Phase::Pending);
}
void DriveChannel::CloseTransport() {
  if (channel) { peer.handle_count = 0; WTSVirtualChannelClose(channel); }
  channel = nullptr;
  event = nullptr;
}
unsigned DriveChannel::Device(unsigned id)
{

  if (!connected) throw std::runtime_error("Drive peer disconnected.");
  auto found = devices.find(id);
  if (found == devices.end()) throw std::runtime_error("Drive removed or peer disconnected.");
  return found->second.wire;
}
int DriveChannel::List(sdlrdp_drive* out, unsigned max)
{
  std::scoped_lock lock(mutex);
  unsigned count = 0;
  for (auto const& [id, device] : devices) {
    if (count == max) break;
    out[count++] = device.drive;
  }
  return int(count);
}
std::shared_ptr<DriveRequest> DriveChannel::Send(unsigned drive, unsigned file, unsigned major,
                                                DrivePacket body, unsigned minor)
{
  std::scoped_lock lock(mutex);
  if (!connected) throw std::runtime_error("Drive channel ended.");
  auto wire = Device(drive);
  auto id = next++;
  if (!id || pending.contains(id)) throw std::runtime_error("Drive completion ids exhausted.");
  auto request = std::make_shared<DriveRequest>();
  request->drive = drive;
  pending.emplace(id, request);
  auto packet = Header(PAKID_CORE_DEVICE_IOREQUEST);
  packet.Put(wire); packet.Put(file); packet.Put(id); packet.Put(major); packet.Put(minor);
  packet.Append(body.bytes);
  try { Write(packet); }
  catch (std::exception const& error) { Fail(error.what()); throw; }
  return request;
}
size_t DriveChannel::WaitAny(std::span<Slot const> slots) {
  Expects(std::ranges::any_of(slots, [](auto const& slot) { return bool(slot.request); }),
    "transfer has outstanding requests");
  std::unique_lock lock(mutex);
  size_t ready = slots.size();
  changed.wait(lock, [&] {
    for (size_t i = 0; i < slots.size(); ++i) {
      if (slots[i].request && (slots[i].request->done || slots[i].request->removed || !connected)) {
        ready = i;
        return true;
      }
    }
    return false;
  });
  return ready;
}
DrivePacket DriveChannel::Wait(std::shared_ptr<DriveRequest> const& request, std::string const& path, bool end)
{
  std::unique_lock lock(mutex);
  changed.wait(lock, [&] { return request->done || request->removed || !connected; });
  if (!connected) throw std::runtime_error("Drive peer disconnected: " + path);
  if (request->removed) throw std::runtime_error("Drive removed: " + path);
  if (request->status && !(end && (request->status == STATUS_NO_MORE_FILES || request->status == STATUS_END_OF_FILE))) {
    // WinPR owns the NTSTATUS name table; unknown client values retain their code.
    auto name = NtStatus2Tag(static_cast<NTSTATUS>(request->status));
    auto status = name ? std::format("{} (0x{:08x})", name, request->status)
                       : std::format("NTSTATUS 0x{:08x}", request->status);
    throw std::runtime_error(std::format("Drive '{}' failed: {}", path, status));
  }
  request->response.origin = weak_from_this();
  return std::move(request->response);
}
}
