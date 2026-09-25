#include <sdl-rdp/drive/information.hpp>

#include <sdl-rdp/drive/exceptions.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::drive::detail::information {
namespace {
// 2024-01-01T00:00:00Z: (1704067200 + 11644473600) seconds of 10^7 FILETIME ticks.
constexpr std::uint64_t NewYear2024     = 133485408000000000;
constexpr std::int64_t  NewYear2024Unix = 1704067200;
constexpr std::uint32_t Directory       = 0x10;
constexpr std::uint32_t Archive         = 0x20;
auto BasicRecord(std::uint64_t written, std::uint32_t attributes) -> DrivePacket {
  DrivePacket packet;
  packet.Write(std::uint32_t{ 36 });
  packet.Write(std::uint64_t{ 1 });
  packet.Write(std::uint64_t{ 2 });
  packet.Write(written);
  packet.Write(std::uint64_t{ 3 });
  packet.Write(attributes);
  return packet;
}
auto Standard(std::uint64_t size) -> DrivePacket {
  DrivePacket packet;
  packet.Write(std::uint32_t{ 22 });
  packet.Write(std::uint64_t{ 4096 });
  packet.Write(size);
  packet.Write(std::uint32_t{ 1 });
  packet.Write(std::uint8_t{ 0 });
  packet.Write(std::uint8_t{ 0 });
  return packet;
}
}
TEST(Basic, ReadsKindAndModificationTime) {
  auto const basic = Basic(Information(BasicRecord(NewYear2024, Archive)));
  EXPECT_FALSE(basic.directory);
  EXPECT_EQ(basic.modified, NewYear2024Unix);
}
TEST(Basic, MarksDirectories) {
  EXPECT_TRUE(Basic(Information(BasicRecord(NewYear2024, Directory | Archive))).directory);
}
TEST(Basic, RefusesAShortRecord) {
  DrivePacket basic;
  basic.Write(std::uint32_t{ 0 });
  EXPECT_THROW(std::ignore = Basic(Information(basic)), MalformedResponse);
}
TEST(EndOfFile, ReadsTheSize) {
  EXPECT_EQ(EndOfFile(Information(Standard(1234))), 1234U);
}
TEST(Information, RefusesALengthPastTheResponse) {
  DrivePacket response;
  response.Write(std::uint32_t{ 36 });
  response.Write(std::uint64_t{ 0 });
  EXPECT_THROW(Information(response), MalformedResponse);
}
TEST(InformationRequest, CarriesTypeLengthPaddingAndBody) {
  DrivePacket body;
  body.Write(std::uint8_t{ 1 });
  auto request = InformationRequest(InformationClass::Disposition, body);
  EXPECT_EQ(request.Bytes().size(), 33U);
  EXPECT_EQ(request.Read<std::uint32_t>(), 13U);
  EXPECT_EQ(request.Read<std::uint32_t>(), 1U);
  request.Skip(24);
  EXPECT_EQ(request.Read<std::uint8_t>(), 1U);
}
TEST(UnixSeconds, CountsFromTheUnixEpoch) {
  EXPECT_EQ(UnixSeconds(116444736000000000), 0);
  EXPECT_EQ(UnixSeconds(NewYear2024 + 9999999), NewYear2024Unix);
}
}
