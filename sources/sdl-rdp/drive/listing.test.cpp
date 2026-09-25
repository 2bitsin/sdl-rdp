#include <sdl-rdp/drive/listing.hpp>

#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::drive {
namespace {
constexpr std::uint32_t Directory   = 0x10;
constexpr std::size_t   EntryHeader = 64;
struct Named {
  std::u16string_view name;
  std::uint64_t       size;
  std::uint32_t       attributes;
};
auto File(std::u16string_view name, std::uint64_t size) -> Named {
  return { .name = name, .size = size, .attributes = 0 };
}
auto Folder(std::u16string_view name) -> Named {
  return { .name = name, .size = 0, .attributes = Directory };
}
auto Record(Named const& entry, bool last) -> DrivePacket {
  auto const  name   = oxbox::utilities::AsBytes(entry.name);
  DrivePacket record;
  record.Write(Backend::Narrowed<std::uint32_t>(last ? 0 : EntryHeader + name.size()));
  record.Write(std::uint32_t{ 0 });
  record.Zero(32);
  record.Write(entry.size);
  record.Write(std::uint64_t{ 4096 });
  record.Write(entry.attributes);
  record.Write(Backend::Narrowed<std::uint32_t>(name.size()));
  record.Append(name);
  return record;
}
// MS-FSCC 2.4.10 FILE_DIRECTORY_INFORMATION records behind the IRP response's Length field.
auto Response(std::initializer_list<Named> entries) -> DrivePacket {
  DrivePacket body;
  std::size_t index = 0;
  for (auto const& entry : entries) body.Append(Record(entry, ++index == entries.size()).Bytes());
  DrivePacket response;
  response.Write(Backend::Narrowed<std::uint32_t>(body.Bytes().size()));
  response.Append(body.Bytes());
  return response;
}
auto Tree() -> DrivePacket {
  return Response({ Folder(u"."), Folder(u".."), File(u"a.txt", 5), Folder(u"b"), File(u"ž.bin", 7) });
}
}
TEST(Entry, ReadsSizeKindAndName) {
  auto packet = Record(File(u"žqs.txt", 42), true);
  auto entry  = Entry(packet);
  EXPECT_EQ(entry.size, 42U);
  EXPECT_EQ(entry.directory, 0);
  EXPECT_EQ(std::string_view(entry.name), "žqs.txt");
  packet = Record(Folder(u"dir"), true);
  EXPECT_EQ(Entry(packet).directory, 1);
}
TEST(Entry, RefusesANameTheAbiCannotHold) {
  std::u16string const long_name(1024, u'a');
  auto                 packet    = Record(File(long_name, 0), true);
  EXPECT_THROW(Entry(packet), EntryNameTooLong);
}
TEST(Listing, SkipsDotEntriesAndTheOffset) {
  std::array<sdlrdp_dirent, 8> out    { };
  Listing                      listing{ 1, out };
  EXPECT_TRUE(listing.Collect(Tree()));
  ASSERT_EQ(listing.Count(), 2U);
  EXPECT_EQ(std::string_view(out[0].name), "b");
  EXPECT_EQ(out[0].directory, 1);
  EXPECT_EQ(std::string_view(out[1].name), "ž.bin");
  EXPECT_EQ(out[1].size, 7U);
  EXPECT_FALSE(listing.Full());
}
TEST(Listing, StopsWhenTheOutputIsFull) {
  std::array<sdlrdp_dirent, 1> out    { };
  Listing                      listing{ 0, out };
  EXPECT_TRUE(listing.Collect(Tree()));
  EXPECT_TRUE(listing.Full());
  EXPECT_EQ(std::string_view(out[0].name), "a.txt");
}
TEST(Listing, AnEmptyResponseEndsTheDirectory) {
  std::array<sdlrdp_dirent, 1> out    { };
  Listing                      listing{ 0, out };
  DrivePacket                  empty;
  empty.Write(std::uint32_t{ 0 });
  EXPECT_FALSE(listing.Collect(empty));
  EXPECT_EQ(listing.Count(), 0U);
}
TEST(Listing, RefusesALengthPastTheResponse) {
  std::array<sdlrdp_dirent, 1> out      { };
  auto                         response = Tree();
  response.Bytes().resize(response.Bytes().size() - 1);
  EXPECT_THROW(Listing(0, out).Collect(response), MalformedResponse);
}
TEST(Listing, RefusesAnEntryOffsetOutsideTheListing) {
  std::array<sdlrdp_dirent, 4> out      { };
  auto                         response = Response({ File(u"a", 1), File(u"b", 1) });
  response.Bytes()[4] = std::byte{ 0xff };
  EXPECT_THROW(Listing(0, out).Collect(response), MalformedResponse);
}
TEST(DirectoryQuery, CarriesThePatternOnlyFirst) {
  std::array const pattern { std::byte{ '*' }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 } };
  auto             first   = DirectoryQuery(true, pattern);
  auto             next    = DirectoryQuery(false, pattern);
  EXPECT_EQ(first.Bytes().size(), 32U + pattern.size());
  EXPECT_EQ(next.Bytes().size(), 32U);
  EXPECT_EQ(first.Read<std::uint32_t>(), 1U);
  EXPECT_EQ(first.Read<std::uint8_t>(), 1U);
  EXPECT_EQ(first.Read<std::uint32_t>(), pattern.size());
  next.Skip(4);
  EXPECT_EQ(next.Read<std::uint8_t>(), 0U);
  EXPECT_EQ(next.Read<std::uint32_t>(), 0U);
}
}
