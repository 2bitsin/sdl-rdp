#include <sdl-rdp/headless-client.test/client-channels.hpp>
#include <sdl-rdp/headless-client.test/drive-checks.hpp>
#include <sdl-rdp/headless-client.test/rdpdr-packets.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <freerdp/channels/rdpdr.h>
#include <array>
#include <cstddef>
#include <future>
#include <string>
#include <utility>
namespace DriveGate {
namespace {
auto SendMalformedDrivePacket(Headless::Client& client) -> void {
  std::array<BYTE, 4> const malformed{ 0x72, 0x44, 0x41, 0x44 };
  ASSERT_TRUE(Headless::SendStaticChannel(client.Instance().get(), RDPDR_CHANNEL_NAME, malformed));
}
auto EmptyBasicInformation(Headless::DriveObserver& observer) -> Backend::DrivePacket {
  auto request = observer.Observed().io.front();
  auto device  = request.Read<uint32_t>();
  request.Skip(4);
  auto id = request.Read<uint32_t>();
  EXPECT_EQ(request.Read<uint32_t>(), IRP_MJ_QUERY_INFORMATION);
  request.Skip(4);
  EXPECT_EQ(request.Read<uint32_t>(), FileBasicInformation);
  auto response = Completion(device, id, STATUS_SUCCESS);
  response.Write(std::uint32_t{ 0 });
  return response;
}
auto CompleteRead(Headless::DriveObserver& observer, size_t index) -> void {
  auto response = ReplyTo(observer.Observed().io[index], STATUS_SUCCESS);
  response.Write(std::uint32_t{ 65536 });
  response.Bytes().resize(response.Bytes().size() + 65536, std::byte{ 'x' });
  EXPECT_TRUE(observer.Send(response));
}
auto AnnounceDriveNames(Headless::DriveObserver& observer) -> void {
  std::string_view const label = "żółw";
  auto                   wide  = Backend::TranscodeRange<std::vector<uint8_t>>(
      std::as_bytes(std::span(label)), { },
      { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little });
  wide.resize(wide.size() + 2);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 100, wide)));
  std::vector<uint8_t> long_name(600, 'x');
  long_name.push_back(0);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 101, long_name)));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 102, std::array<uint8_t, 2>{ 0xff, 0 })));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_PRINT, 103, { })));
}
}

namespace {
auto ReadLargeFile(sdlrdp_handle* handle, sdlrdp_file* file) -> int {
  std::string bytes(static_cast<std::ptrdiff_t>(3 * 1024) * 1024, '\0');
  return sdlrdp_drive_read(handle, file, 0, bytes.data(), bytes.size());
}
auto StatWithError(sdlrdp_handle* handle, sdlrdp_file* file) -> std::pair<int, std::string> {
  sdlrdp_stat info   { };
  auto        result = sdlrdp_drive_fstat(handle, file, &info);
  return { result, sdlrdp_last_error() };
}
}
namespace {
class DriveWire : public DriveChecks {
protected:
  auto ThenRecoverableAnnouncements(Headless::DriveObserver& observer) -> void {
    auto rejected = std::ranges::find(observer.Observed().replies, 103u, &std::pair<unsigned, unsigned>::first);
    ASSERT_NE(rejected, observer.Observed().replies.end());
    EXPECT_EQ(rejected->second, STATUS_NOT_SUPPORTED);
    EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "truncating"), 1u);
    EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Using DOS name"), 1u);
    EXPECT_EQ(Logged(SDLRDP_LOG_INFO, "extended PDU"), 1u);
  }
  auto ThenDriveNames() -> void {
    std::array<sdlrdp_drive, 8> drives{ };
    ASSERT_EQ(sdlrdp_drive_list(handle.Handle(), drives.data(), 8), 4);
    EXPECT_STREQ(drives[1].name, "żółw");
    EXPECT_EQ(std::string(drives[2].name), std::string(511, 'x'));
    EXPECT_STREQ(drives[3].name, "dos");
  }
  auto ThenHeldFileClosed(Headless::DriveObserver& observer, sdlrdp_file* file) -> void {
    observer.Observed().hold = false;
    auto close = std::async(std::launch::async, [&] { return sdlrdp_drive_close(handle.Handle(), file); });
    ASSERT_TRUE(client->Until([&] { return close.wait_for(0s) == std::future_status::ready; }));
    EXPECT_EQ(close.get(), 0);
  }
  auto WhenReadWindowRefilled(Headless::DriveObserver& observer) -> void {
    ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 8; }));
    CompleteRead(observer, 7);
    ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 9; }));
    CompleteRead(observer, 0);
    ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 10; }));
    std::ranges::for_each(std::views::iota(1uz, 10uz) | std::views::filter([](size_t index) { return index != 7; }),
                          [&](size_t index) { CompleteRead(observer, index); });
  }
  auto ThenTruncatedInformation(auto& stat) -> void {
    auto [result, error] = stat.get();
    EXPECT_EQ(result, -1);
    EXPECT_EQ(error, "Truncated drive response.");
  }
  auto ThenAbortedRead(std::future<int>& read, sdlrdp_file* file) -> void {
    ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(read.get(), -1);
    EXPECT_EQ(Logged(SDLRDP_LOG_WARN, "Drive channel ended: Truncated drive response."), 1u);
    EXPECT_EQ(sdlrdp_drive_close(handle.Handle(), file), -1);
  }
};
TEST_F(DriveWire, MalformedChannelKeepsVideoSession) {
  Write("file", Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024));
  auto* file = Open("file");
  ASSERT_NE(file, nullptr);
  ASSERT_NO_FATAL_FAILURE(HoldRequests());
  auto& observer = *this->observer;
  auto  read     = std::async(std::launch::async, [&] { return ReadLargeFile(handle.Handle(), file); });
  ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 8; }));
  ASSERT_NO_FATAL_FAILURE(SendMalformedDrivePacket(*client));
  ASSERT_TRUE(client->Until([&] {
    sdlrdp_drive value{ };
    return sdlrdp_drive_list(handle.Handle(), &value, 1) == 0;
  }));
  ASSERT_NO_FATAL_FAILURE(ThenAbortedRead(read, file));
  ThenRemovedDrive();
  ThenVideoMatches();
}

TEST_F(DriveWire, MalformedInformationKeepsVideoSession) {
  ASSERT_NO_FATAL_FAILURE(GivenHeldFile());
  auto* file     = held_file;
  auto& observer = *this->observer;
  auto  stat     = std::async(std::launch::async, StatWithError, handle.Handle(), file);
  ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 1; }));
  auto response = EmptyBasicInformation(observer);
  auto warnings = Logged(SDLRDP_LOG_WARN, "");
  ASSERT_TRUE(observer.Send(response));
  ASSERT_TRUE(client->Until([&] { return stat.wait_for(0s) == std::future_status::ready; }));
  ASSERT_NO_FATAL_FAILURE(ThenTruncatedInformation(stat));
  ThenDriveFailure(file, warnings);
  ASSERT_NO_FATAL_FAILURE(ThenVideoMatches());
  RecordProperty("trace", "FileBasicInformation: Length=0; fstat=-1; Truncated drive response.; "
                          "drives=0; WARN=1: Drive channel ended: Truncated drive response.; video matches");
}

TEST_F(DriveWire, SlidingWindowRefillsOnOutOfOrderCompletion) {
  ASSERT_NO_FATAL_FAILURE(GivenHeldFile());
  auto*       file     = held_file;
  auto&       observer = *this->observer;
  std::string bytes(10uz * 65536, '\0');
  auto read = std::async(std::launch::async,
                         [&] { return sdlrdp_drive_read(handle.Handle(), file, 0, bytes.data(), bytes.size()); });
  ASSERT_NO_FATAL_FAILURE(WhenReadWindowRefilled(observer));
  ASSERT_TRUE(client->Until([&] { return read.wait_for(0s) == std::future_status::ready; }));
  EXPECT_EQ(read.get(), int(bytes.size()));
  EXPECT_EQ(bytes, std::string(bytes.size(), 'x'));
  ThenHeldFileClosed(observer, file);
}

TEST_F(DriveWire, UnicodeWireNameAndRecoverableAnnouncements) {
  ASSERT_NO_FATAL_FAILURE(HoldRequests());
  auto& observer = *this->observer;
  ASSERT_NO_FATAL_FAILURE(AnnounceDriveNames(observer));
  ASSERT_TRUE(client->Until([&] {
    return std::ranges::any_of(observer.Observed().replies, [](auto const& reply) { return reply.first == 103; });
  }));
  ASSERT_NO_FATAL_FAILURE(ThenDriveNames());
  ThenRecoverableAnnouncements(observer);
}
TEST_F(DriveWire, UnknownCompletionIsIgnored) {
  pump.request_stop();
  pump.join();
  Headless::DriveObserver const observer(*client);
  Backend::DrivePacket          packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(std::uint16_t{ PAKID_CORE_DEVICE_IOCOMPLETION });
  packet.Write(std::uint32_t{ 0 });
  packet.Write(std::uint32_t{ UINT32_MAX });
  packet.Write(std::uint32_t{ STATUS_SUCCESS });
  ASSERT_TRUE(observer.Send(packet));
  ASSERT_TRUE(client->Until([&] { return Logged(SDLRDP_LOG_WARN, "Unknown drive completion id") == 1; }));
  sdlrdp_drive value{ };
  EXPECT_EQ(sdlrdp_drive_list(handle.Handle(), &value, 1), 1);
  ThenVideoMatches();
}
}
}
