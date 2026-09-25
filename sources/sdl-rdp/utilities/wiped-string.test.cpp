#include <sdl-rdp/utilities/wiped-string.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::utilities::detail::wiped_string {
using sdl_rdp::utilities::Wipe;
using sdl_rdp::utilities::WipedString;
namespace {
constexpr std::string_view Short{ "hunter2"                                        };
constexpr std::string_view Long { "a long password beyond the small string buffer" };

auto Zeroed(std::span<char const> storage) -> bool {
  return std::ranges::all_of(storage, [](char byte) { return byte == '\0'; });
}
auto Holds(std::span<char const> storage, std::string_view text) -> bool {
  return !std::ranges::search(storage, text).empty();
}
auto Inside(WipedString const& object, std::span<char const> storage) -> bool {
  auto const bytes = std::as_bytes(std::span{ &object, 1 });
  auto const text  = std::as_bytes(storage);
  return std::less_equal{ }(bytes.data(), text.data()) && std::less{ }(text.data(), bytes.data() + bytes.size());
}
// The heap text lives in this buffer, which outlives the string and stays readable after its release.
class HeapStorage {
public:
  auto Allocator() -> WipedString::Allocator {
    return WipedString::Allocator{ &_resource };
  }
  auto Bytes() const -> std::span<char const> {
    return _bytes;
  }

private:
  std::array<char, 256>               _bytes   { };
  std::pmr::monotonic_buffer_resource _resource{ _bytes.data(), _bytes.size(), std::pmr::null_memory_resource() };
};
// While it lives, every default-resource allocation throws, so a move that allocates is a test failure.
class NoDefaultResource {
public:
  NoDefaultResource() : _previous{ *std::pmr::set_default_resource(std::pmr::null_memory_resource()) } { }
  NoDefaultResource(NoDefaultResource const&) = delete;
  NoDefaultResource(NoDefaultResource&&)      = delete;
  ~NoDefaultResource() {
    std::pmr::set_default_resource(&_previous.get());
  }
  auto operator=(NoDefaultResource const&) -> NoDefaultResource& = delete;
  auto operator=(NoDefaultResource&&)      -> NoDefaultResource& = delete;

private:
  std::reference_wrapper<std::pmr::memory_resource> _previous;
};
}

TEST(Wipe, OverwritesEveryByte) {
  std::array<std::byte, 4> bytes{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };
  Wipe(bytes);
  EXPECT_EQ(bytes, (std::array<std::byte, 4>{ }));
}
TEST(WipedString, HoldsTheTextItWasGiven) {
  WipedString const secret{ Short };
  std::string const text  { "abc" };
  WipedString const copied{ text  };
  EXPECT_EQ(secret.Text(), Short);
  EXPECT_EQ(copied.Text(), "abc");
}
TEST(WipedString, CopiesAndMovesCarryTheText) {
  WipedString       source{ Long              };
  WipedString const copy  { source            };
  WipedString const moved { std::move(source) };
  EXPECT_EQ(copy.Text(), Long);
  EXPECT_EQ(moved.Text(), Long);
}
TEST(WipedString, AssignmentReplacesTheText) {
  WipedString       first { "first"  };
  WipedString const second{ "second" };
  first = second;
  EXPECT_EQ(first.Text(), "second");
  first = WipedString{ "third" };
  EXPECT_EQ(first.Text(), "third");
}
TEST(WipedString, MoveWipesAShortSourceInPlace) {
  WipedString source  { Short };
  auto const  storage = std::span<char const>{ source.Text() };
  EXPECT_TRUE(Inside(source, storage)) << "the short text sits inside the object";
  WipedString const moved{ std::move(source) };
  EXPECT_EQ(moved.Text(), Short);
  EXPECT_TRUE(Zeroed(storage));
}
TEST(WipedString, MoveTakesTheLongSourceBufferAndWipesItOnRelease) {
  HeapStorage heap;
  WipedString source  { Long, heap.Allocator() };
  auto const  storage = std::span<char const>{ source.Text() };
  {
    WipedString const moved{ std::move(source) };
    EXPECT_EQ(moved.Text(), Long);
    EXPECT_EQ(moved.Text().data(), storage.data()) << "the buffer moves with its text";
    EXPECT_TRUE(Holds(heap.Bytes(), Long));
  }
  EXPECT_TRUE(Zeroed(heap.Bytes()));
}
TEST(WipedString, MoveAllocatesNothing) {
  WipedString             long_source   { Long                   };
  WipedString             short_source  { Short                  };
  auto const              inline_bytes  = std::span<char const>{ short_source.Text() };
  NoDefaultResource const no_allocation;
  WipedString const       moved_long    { std::move(long_source) };
  WipedString             assigned      { Short                  };
  assigned = std::move(short_source);
  EXPECT_EQ(moved_long.Text(), Long);
  EXPECT_EQ(assigned.Text(), Short);
  EXPECT_TRUE(Zeroed(inline_bytes));
}
TEST(WipedString, DestructionWipesAShortTextInPlace) {
  std::optional<WipedString> holder  { std::in_place, Short };
  auto const                 storage = std::span<char const>{ holder->Text() };
  holder.reset();
  EXPECT_TRUE(Zeroed(storage));
}
TEST(WipedString, DestructionWipesALongTextBuffer) {
  HeapStorage heap;
  {
    WipedString const secret{ Long, heap.Allocator() };
    EXPECT_FALSE(Zeroed(heap.Bytes()));
  }
  EXPECT_TRUE(Zeroed(heap.Bytes()));
}
TEST(WipedString, MoveAssignmentWipesTheSourceAndTheOldText) {
  HeapStorage heap;
  WipedString target  { Long, heap.Allocator() };
  WipedString source  { Short                  };
  auto const  storage = std::span<char const>{ source.Text() };
  EXPECT_TRUE(Holds(heap.Bytes(), Long));
  target = std::move(source);
  EXPECT_EQ(target.Text(), Short);
  EXPECT_TRUE(Zeroed(storage));
  EXPECT_FALSE(Holds(heap.Bytes(), Long));
}
}
