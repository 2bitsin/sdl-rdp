#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/drive/capabilities.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/label.hpp>
#include <sdl-rdp/drive/records.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <algorithm>
#include <array>
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
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::drive::Drive;
using sdl_rdp::freerdp_facade::CapabilityType;
using sdl_rdp::freerdp_facade::Component;
using sdl_rdp::freerdp_facade::DeviceType;
using sdl_rdp::freerdp_facade::DriveChannelName;
using sdl_rdp::freerdp_facade::ExtendedPdu;
using sdl_rdp::freerdp_facade::Name;
using sdl_rdp::freerdp_facade::PacketId;
using sdl_rdp::freerdp_facade::ProtocolMajor;
using sdl_rdp::freerdp_facade::ProtocolMinorRdp6x;
using sdl_rdp::link::DriveChanged;
using sdl_rdp::link::Event;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Required;
namespace {
auto Header(PacketId type) -> DrivePacket {
  DrivePacket packet;
  packet.Write(Component::Core);
  packet.Write(type);
  return packet;
}
auto Announcement(PacketId type, std::uint32_t client_id) -> DrivePacket {
  auto packet = Header(type);
  packet.Write(ProtocolMajor);
  packet.Write(ProtocolMinorRdp6x);
  packet.Write(client_id);
  return packet;
}
auto IoRequest(std::span<std::uint32_t const> header, DrivePacket const& body) -> DrivePacket {
  auto packet = Header(PacketId::DeviceIoRequest);
  std::ranges::for_each(header, [&packet](std::uint32_t field) { packet.Write(field); });
  packet.Append(body.Bytes());
  return packet;
}
auto DriveEvent(bool added, Drive const& drive) -> Event {
  return DriveChanged{ .added = added, .id = drive.id, .name = drive.name };
}
// MS-RDPEFS 2.2.2.6: the confirmed id follows the client's version.
auto ConfirmedClientId(DrivePacket& packet) -> std::uint32_t {
  packet.Skip(sizeof(std::uint16_t) * 2);
  return packet.Read<std::uint32_t>();
}
// A listing or a read that reaches its end completes with NoMoreFiles or EndOfFile, which the caller expects.
auto Failed(NtStatus status, bool end) -> bool {
  switch (status) {
  case NtStatus::Success: return false;
  case NtStatus::NoMoreFiles:
  case NtStatus::EndOfFile: return !end;
  default:                  return true;
  }
}
// Logs why the channel ends while its peer is connected; the caller shuts the channel down after it.
auto Ending(DriveChannel const& channel) -> auto {
  return [&channel](std::string_view cause) { channel.Warn(std::format("Drive channel ended: {}", cause)); };
}
}
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
auto DriveChannel::Event() const -> std::optional<WaitHandle> {
  return event;
}
auto DriveChannel::Open() -> bool {
  Expects(!channel, "drive channel opens once");
  auto const opened = [this] {
    channel = _link.Channels().Open(DriveChannelName);
    event   = channel->Handle();
    auto packet = Announcement(PacketId::ServerAnnounce, client_id);
    Write(packet);
    return true;
  };
  if (Contained(false, opened, Ending(*this))) return true;
  Shutdown();
  return false;
}
auto DriveChannel::Write(DrivePacket& packet) -> void {
  Expects(channel.has_value(), "drive transport exists");
  if (!channel || !channel->Write(packet.Bytes())) throw TransportDisconnected{ };
  _link.Signal();
}
auto DriveChannel::Capabilities() -> void {
  auto                    packet           = Header(PacketId::ServerCapability);
  constexpr std::uint32_t capability_count = 2;
  packet.Write(std::uint16_t{ capability_count });
  packet.Write(std::uint16_t{ 0 });
  GeneralCapability(packet);
  DriveCapability(packet);
  Write(packet);
  packet = Announcement(PacketId::ClientIdConfirm, client_id);
  Write(packet);
  auto logged_on = Header(PacketId::UserLoggedOn);
  Write(logged_on);
}
auto DriveChannel::Announce(DrivePacket& packet) -> void {
  auto count = packet.Read<std::uint32_t>();
  while (count--) {
    auto                type = packet.Read<DeviceType>();
    auto                wire = packet.Read<std::uint32_t>();
    std::array<char, 9> name { };
    for (std::size_t i = 0; i < 8; ++i) name[i] = static_cast<char>(packet.Read<std::uint8_t>());
    auto length = packet.Read<std::uint32_t>();
    auto begin  = packet.Position();
    packet.Skip(length);
    auto response = Header(PacketId::DeviceReply);
    response.Write(wire);
    response.Write(type == DeviceType::Filesystem ? NtStatus::Success : NtStatus::NotSupported);
    Write(response);
    if (type != DeviceType::Filesystem) continue;
    auto label = Label(std::span(packet.Bytes()).subspan(begin, length), name.data());
    AnnounceDevice(wire, std::move(label));
  }
}
auto DriveChannel::ClientCapabilities(DrivePacket& packet) -> void {
  constexpr std::size_t capability_header_size = 8;
  auto                  count                  = packet.Read<std::uint16_t>();
  packet.Skip(2);
  while (count--) {
    auto start   = packet.Position();
    auto type    = packet.Read<CapabilityType>();
    auto length  = packet.Read<std::uint16_t>();
    auto version = packet.Read<CapabilityVersion>();
    if (length < capability_header_size) throw ShortCapability{ std::to_underlying(type), length };
    packet.Skip(length - capability_header_size);
    auto end = packet.Position();
    // The default: printer, port and smartcard capabilities are valid (MS-RDPEFS 2.2.1.2) and none is redirected.
    switch (type) {
    case CapabilityType::Drive:   drive_version = version; break;
    case CapabilityType::General: GeneralClientCapability(packet, start, length, version); break;
    default:                      break;
    }
    packet.Seek(end);
  }
}
auto DriveChannel::Warn(std::string_view cause) const -> void {
  if (connected) _diagnostics.Log(LogLevel::Warn, std::string{ cause });
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
  auto status = packet.Read<NtStatus>();
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
  if (packet.Read<Component>() != Component::Core) return;
  // The default: server-direction ids (announce, capabilities, reply, I/O request, logon) never come from a client.
  switch (packet.Read<PacketId>()) {
  case PacketId::ClientIdConfirm:    client_id = ConfirmedClientId(packet); break;
  case PacketId::ClientCapability:   ClientCapabilities(packet); break;
  case PacketId::ClientName:         Capabilities(); break;
  case PacketId::DeviceListAnnounce: Announce(packet); break;
  case PacketId::DeviceIoCompletion: Complete(packet); break;
  case PacketId::DeviceListRemove:
    for (auto count = packet.Read<std::uint32_t>(); count > 0; --count) Remove(packet.Read<std::uint32_t>());
    break;
  default: break;
  }
}
auto DriveChannel::Pump(Signalled const& signaled) -> bool {
  std::scoped_lock const lock(mutex);
  if (!connected) {
    CloseTransport();
    return true;
  }
  if (!signaled.Contains(event)) return true;
  auto const pumped = Contained(
      std::optional<bool>{ }, [this] -> std::optional<bool> { return PumpAvailable(); }, Ending(*this));
  if (pumped) return *pumped;
  Shutdown();
  return true;
}
auto DriveChannel::Send(std::uint32_t drive, std::uint32_t file, IrpMajor major, DrivePacket const& body,
                        IrpMinor minor) -> std::shared_ptr<DriveRequest> {
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
auto DriveChannel::AnnounceDevice(std::uint32_t wire, std::string label) -> void {
  auto const  id     = _session.NextDrive();
  DeviceEntry entry  { .wire = wire, .drive = { .id = id, .name = std::move(label) } };
  auto const& stored = devices.emplace(id, std::move(entry)).first->second;
  _events.Push(DriveEvent(true, stored.drive));
}
auto DriveChannel::GeneralClientCapability(DrivePacket& packet, std::size_t start, std::size_t length,
                                           CapabilityVersion version) const -> void {
  constexpr std::size_t general_caps_v1_size          = 40;
  constexpr std::size_t protocol_major_version_offset = 16;
  constexpr std::size_t io_code_fields_size           = 8;
  if (length < general_caps_v1_size) throw ShortCapability{ std::to_underlying(CapabilityType::General), length };
  packet.Seek(start + protocol_major_version_offset);
  auto major = packet.Read<std::uint16_t>();
  auto minor = packet.Read<std::uint16_t>();
  packet.Skip(io_code_fields_size);
  auto const extended = packet.Read<ExtendedPdu>();
  _diagnostics.Log(
      LogLevel::Info,
      std::format("Drive client version {}.{}, general capability {}, extended PDU 0x{:08x}, device removal {}.", major,
                  minor, std::to_underlying(version), std::to_underlying(extended),
                  Has(extended, ExtendedPdu::DeviceRemove)));
}
auto DriveChannel::PumpAvailable() -> bool {
  auto const ready = Required(event, "a pumped drive channel has its event");
  Expects(channel.has_value(), "a pumped drive channel is open");
  if (!channel) throw DriveChannelFailed{ "read" };
  auto& open = *channel;
  for (;;) {
    if (!ready.Signalled()) return true;
    auto const pending = open.Pending();
    if (!pending) throw DriveChannelFailed{ "read" };
    if (!*pending) return true;
    DrivePacket packet;
    packet.Bytes().resize(*pending);
    auto const read = open.Read(packet.Bytes());
    if (!read) throw DriveChannelFailed{ "read" };
    packet.Bytes().resize(*read);
    Receive(packet);
  }
}
auto DriveChannel::Label(std::span<std::byte const> bytes, std::string_view dos) const -> std::string {
  return Contained(
      std::string{ dos }, [&] { return DecodeLabel(bytes, drive_version, dos); },
      [&](std::string_view cause) { Warn(std::format("{} Using DOS name '{}'.", cause, dos)); });
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
  event.reset();
}
auto DriveChannel::Device(std::uint32_t id) -> std::uint32_t {
  Expects(connected, "a request addresses a connected peer");
  auto found = devices.find(id);
  if (found == devices.end()) throw DriveRemoved{ "the requested drive" };
  return found->second.wire;
}
auto DriveChannel::List() -> std::vector<Drive> {
  std::scoped_lock const lock(mutex);
  return std::ranges::to<std::vector>(devices | std::views::values | std::views::transform(&DeviceEntry::drive));
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
  if (Failed(request->status, end))
    throw StatusFailure{ path, Name(request->status), std::to_underlying(request->status) };
  request->response.Origin(weak_from_this());
  return std::move(request->response);
}
}
