#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/records.hpp>
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/drive/checks.hpp>
#include <sdl-rdp/headless-client.test/drive/rdpdr-packets.hpp>
#include <sdl-rdp/headless-client.test/utilities/thrown-text.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/text.hpp>

#include <freerdp/channels/rdpdr.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <tuple>
#include <utility>
namespace sdl_rdp::integration::drive_test::detail::wire {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::drive::File;
using namespace std::chrono_literals;
using sdl_rdp::drive::DrivePacket;
using sdl_rdp::drive::MalformedResponse;
using sdl_rdp::drive::PeerDisconnected;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::SendStaticChannel;
using sdl_rdp::headless_client_test::drive::Completion;
using sdl_rdp::headless_client_test::drive::DeviceAnnouncement;
using sdl_rdp::headless_client_test::drive::DriveChecks;
using sdl_rdp::headless_client_test::drive::DriveObserver;
using sdl_rdp::headless_client_test::drive::Pattern;
using sdl_rdp::headless_client_test::drive::ReadAt;
using sdl_rdp::headless_client_test::drive::ReplyTo;
using sdl_rdp::headless_client_test::utilities::ThrownText;
using sdl_rdp::utilities::TranscodeRange;

namespace {
auto SendMalformedDrivePacket(Client& client) -> void {
  std::array<std::uint8_t, 4> const malformed{ 0x72, 0x44, 0x41, 0x44 };
  ASSERT_TRUE(SendStaticChannel(*client.Instance(), RDPDR_CHANNEL_NAME, std::as_bytes(std::span(malformed))));
}
auto EmptyBasicInformation(DriveObserver& observer) -> DrivePacket {
  auto request = observer.Observed().io.front();
  auto device  = request.Read<std::uint32_t>();
  request.Skip(4);
  auto id = request.Read<std::uint32_t>();
  EXPECT_EQ(request.Read<std::uint32_t>(), IRP_MJ_QUERY_INFORMATION);
  request.Skip(4);
  EXPECT_EQ(request.Read<std::uint32_t>(), FileBasicInformation);
  auto response = Completion(device, id, STATUS_SUCCESS);
  response.Write(std::uint32_t{ 0 });
  return response;
}
auto CompleteRead(DriveObserver& observer, std::size_t index) -> void {
  auto response = ReplyTo(observer.Observed().io[index], STATUS_SUCCESS);
  response.Write(std::uint32_t{ 65536 });
  response.Bytes().resize(response.Bytes().size() + 65536, std::byte{ 'x' });
  EXPECT_TRUE(observer.Send(response));
}
auto AnnounceDriveNames(DriveObserver& observer) -> void {
  std::string_view const label = "żółw";
  auto                   wide  = TranscodeRange<std::vector<std::byte>>(
      std::as_bytes(std::span(label)), { },
      { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little });
  wide.resize(wide.size() + 2);
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 100, wide)));
  std::vector<std::byte> long_name(600, std::byte{ 'x' });
  long_name.push_back(std::byte{ 0 });
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 101, long_name)));
  ASSERT_TRUE(
      observer.Send(DeviceAnnouncement(RDPDR_DTYP_FILESYSTEM, 102, std::array{ std::byte{ 0xff }, std::byte{ 0 } })));
  ASSERT_TRUE(observer.Send(DeviceAnnouncement(RDPDR_DTYP_PRINT, 103, { })));
}
auto ReadLargeFile(File& file) -> std::size_t {
  std::string bytes(static_cast<std::ptrdiff_t>(3 * 1024) * 1024, '\0');
  return ReadAt(file, 0, bytes);
}
auto StatWithError(File& file) -> std::string {
  return ThrownText<MalformedResponse>([&] { return file.Stat(); });
}
class DriveWire : public DriveChecks {
protected:
  auto ThenRecoverableAnnouncements(DriveObserver& observer) -> void {
    auto rejected = std::ranges::find(observer.Observed().replies, 103u,
                                      &std::pair<std::uint32_t, std::uint32_t>::first);
    ASSERT_NE(rejected, observer.Observed().replies.end());
    EXPECT_EQ(rejected->second, STATUS_NOT_SUPPORTED);
    EXPECT_EQ(logs.Count(LogLevel::Warn, "Using DOS name"), 1u);
    EXPECT_EQ(logs.Count(LogLevel::Info, "extended PDU"), 1u);
  }
  auto ThenDriveNames() -> void {
    auto const drives = Files().List();
    ASSERT_EQ(drives.size(), 4u);
    EXPECT_EQ(drives[1].name, "żółw");
    EXPECT_EQ(drives[2].name, std::string(600, 'x'));
    EXPECT_EQ(drives[3].name, "dos");
  }
  auto ThenHeldFileClosed(DriveObserver& observer, File& file) -> void {
    observer.Observed().hold = false;
    auto close = std::async(std::launch::async, [&] { file.Close(); });
    ASSERT_TRUE(client->Until([&] { return close.wait_for(0s) == std::future_status::ready; }));
    close.get();
  }
  auto WhenReadWindowRefilled(DriveObserver& observer) -> void {
    ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 8; }));
    CompleteRead(observer, 7);
    ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 9; }));
    CompleteRead(observer, 0);
    ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 10; }));
    std::ranges::for_each(std::views::iota(1uz, 10uz)
                              | std::views::filter([](std::size_t index) { return index != 7; }),
                          [&](std::size_t index) { CompleteRead(observer, index); });
  }
  auto ThenTruncatedInformation(auto& stat) -> void {
    EXPECT_EQ(stat.get(), "Malformed drive response: truncated.");
  }
  static auto ThenAbortedRead(std::future<std::size_t>& read) -> void {
    ASSERT_EQ(read.wait_for(2s), std::future_status::ready);
    EXPECT_THROW(std::ignore = read.get(), PeerDisconnected);
  }
  auto ThenEndedChannel(File& file) -> void {
    EXPECT_EQ(logs.Count(LogLevel::Warn, "Drive channel ended: Malformed drive response: truncated."), 1u);
    EXPECT_THROW(file.Close(), PeerDisconnected);
  }
};
TEST_F(DriveWire, MalformedChannelKeepsVideoSession) {
  Write("file", Pattern(static_cast<std::ptrdiff_t>(3 * 1024) * 1024));
  auto const opened = Open("file");
  auto&      file   = *opened;
  ASSERT_NO_FATAL_FAILURE(HoldRequests());
  auto& observer = *this->observer;
  auto  read     = std::async(std::launch::async, [&] { return ReadLargeFile(file); });
  ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 8; }));
  ASSERT_NO_FATAL_FAILURE(SendMalformedDrivePacket(*client));
  ASSERT_TRUE(client->Until([&] { return Files().List().empty(); }));
  ASSERT_NO_FATAL_FAILURE(ThenAbortedRead(read));
  ASSERT_NO_FATAL_FAILURE(ThenEndedChannel(file));
  ThenRemovedDrive();
  ThenVideoMatches();
}

TEST_F(DriveWire, MalformedInformationKeepsVideoSession) {
  ASSERT_NO_FATAL_FAILURE(GivenHeldFile());
  ASSERT_TRUE(held_file) << "the held file is open";
  auto& file     = *held_file;
  auto& observer = *this->observer;
  auto  stat     = std::async(std::launch::async, StatWithError, std::ref(file));
  ASSERT_TRUE(client->Until([&] { return observer.Observed().requests == 1; }));
  auto response = EmptyBasicInformation(observer);
  auto warnings = logs.Count(LogLevel::Warn, "");
  ASSERT_TRUE(observer.Send(response));
  ASSERT_TRUE(client->Until([&] { return stat.wait_for(0s) == std::future_status::ready; }));
  ASSERT_NO_FATAL_FAILURE(ThenTruncatedInformation(stat));
  ThenDriveFailure(file, warnings);
  ASSERT_NO_FATAL_FAILURE(ThenVideoMatches());
  RecordProperty("trace", "FileBasicInformation: Length=0; fstat=-1; Malformed drive response: truncated.; "
                          "drives=0; WARN=1: Drive channel ended: Malformed drive response: truncated.; video matches");
}

TEST_F(DriveWire, SlidingWindowRefillsOnOutOfOrderCompletion) {
  ASSERT_NO_FATAL_FAILURE(GivenHeldFile());
  ASSERT_TRUE(held_file) << "the held file is open";
  auto&       file     = *held_file;
  auto&       observer = *this->observer;
  std::string bytes(10uz * 65536, '\0');
  auto        read     = std::async(std::launch::async, [&] { return ReadAt(file, 0, bytes); });
  ASSERT_NO_FATAL_FAILURE(WhenReadWindowRefilled(observer));
  ASSERT_TRUE(client->Until([&] { return read.wait_for(0s) == std::future_status::ready; }));
  EXPECT_EQ(read.get(), bytes.size());
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
  DriveObserver observer(*client);
  DrivePacket   packet;
  packet.Write(std::uint16_t{ RDPDR_CTYP_CORE });
  packet.Write(std::uint16_t{ PAKID_CORE_DEVICE_IOCOMPLETION });
  packet.Write(std::uint32_t{ 0 });
  packet.Write(std::uint32_t{ UINT32_MAX });
  packet.Write(std::uint32_t{ STATUS_SUCCESS });
  ASSERT_TRUE(observer.Send(packet));
  ASSERT_TRUE(client->Until([&] { return logs.Count(LogLevel::Warn, "Unknown drive completion id") == 1; }));
  EXPECT_EQ(Files().List().size(), 1u);
  ThenVideoMatches();
}
}
}
