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
#include <utility>
#include <vector>

namespace sdl_rdp::drive::detail::listing {
using sdl_rdp::utilities::Narrowed;

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
  record.Write(Narrowed<std::uint32_t>(last ? 0 : EntryHeader + name.size()));
  record.Write(std::uint32_t{ 0 });
  record.Zero(32);
  record.Write(entry.size);
  record.Write(std::uint64_t{ 4096 });
  record.Write(entry.attributes);
  record.AppendCounted(name);
  return record;
}
// MS-FSCC 2.4.10 FILE_DIRECTORY_INFORMATION records behind the IRP response's Length field.
auto Response(std::initializer_list<Named> entries) -> DrivePacket {
  DrivePacket body;
  std::size_t index = 0;
  for (auto const& entry : entries) body.Append(Record(entry, ++index == entries.size()).Bytes());
  DrivePacket response;
  response.AppendCounted(body.Bytes());
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
  EXPECT_FALSE(entry.directory);
  EXPECT_EQ(entry.name, "žqs.txt");
  packet = Record(Folder(u"dir"), true);
  EXPECT_TRUE(Entry(packet).directory);
}
TEST(Entry, ReadsANameOfAnyLength) {
  std::u16string const long_name(1024, u'a');
  auto                 packet    = Record(File(long_name, 0), true);
  EXPECT_EQ(Entry(packet).name.size(), 1024U);
}
TEST(Listing, SkipsDotEntriesAndTheOffset) {
  Listing listing{ 1, 8 };
  EXPECT_TRUE(listing.Collect(Tree()));
  EXPECT_FALSE(listing.Full());
  auto const out = std::move(listing).Entries();
  ASSERT_EQ(out.size(), 2U);
  EXPECT_EQ(out[0].name, "b");
  EXPECT_TRUE(out[0].directory);
  EXPECT_EQ(out[1].name, "ž.bin");
  EXPECT_EQ(out[1].size, 7U);
}
TEST(Listing, StopsWhenTheOutputIsFull) {
  Listing listing{ 0, 1 };
  EXPECT_TRUE(listing.Collect(Tree()));
  EXPECT_TRUE(listing.Full());
  EXPECT_EQ(std::move(listing).Entries().front().name, "a.txt");
}
TEST(Listing, AnEmptyResponseEndsTheDirectory) {
  Listing     listing{ 0, 1 };
  DrivePacket empty;
  empty.Write(std::uint32_t{ 0 });
  EXPECT_FALSE(listing.Collect(empty));
  EXPECT_TRUE(std::move(listing).Entries().empty());
}
TEST(Listing, RefusesALengthPastTheResponse) {
  auto response = Tree();
  response.Bytes().resize(response.Bytes().size() - 1);
  EXPECT_THROW(Listing(0, 1).Collect(response), MalformedResponse);
}
TEST(Listing, RefusesAnEntryOffsetOutsideTheListing) {
  auto response = Response({ File(u"a", 1), File(u"b", 1) });
  response.Bytes()[4] = std::byte{ 0xff };
  EXPECT_THROW(Listing(0, 4).Collect(response), MalformedResponse);
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
