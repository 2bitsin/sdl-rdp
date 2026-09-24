#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace fixture {
enum class Phase : std::uint8_t { DOWN = 0, MOVE = 1, UP = 2 };
enum class Level {
  ERROR       = 1,
  WARN        = 2,
  INFORMATION = 3,
};
struct Extent {
  std::uint32_t width;
  std::uint32_t height;
};
class Pointer {
public:
       Pointer(std::string_view name, std::int32_t x, std::int32_t y, bool visible);
       Pointer(Pointer const&)                                    = delete;
       Pointer(Pointer&&)                                         = delete;
       ~Pointer()                                                 = default;
  auto operator=(Pointer const&)                      -> Pointer& = delete;
  auto operator=(Pointer&&)                           -> Pointer& = delete;
  auto Place(std::int32_t x, std::int32_t y) noexcept -> void;
  auto Position() const noexcept                      -> std::pair<std::int32_t, std::int32_t>;
  auto Visible() const                                -> bool;
protected:
  auto Name() const -> std::string_view;
private:
  std::string_view _name;
  std::int32_t     _x      { 128   };
  std::int32_t     _y      { 80    };
  bool             _visible{ false };
};
constexpr auto SCALE   { 3                            };
constexpr auto CHANNELS{ static_cast<std::int32_t>(2) };
constexpr auto RATE    { std::uint32_t{ 60 }          };

constexpr Extent DESKTOP{ .width = 1280, .height = 800 };

constexpr std::array<std::pair<std::string_view, std::int32_t>, 3> LEVELS{ {
    { "error"      , 1 },
    { "warning"    , 2 },
    { "information", 3 },
} };

constexpr std::string_view USAGE = "usage: fixture [--scale N] [--channels N] [--rate HZ] "
                                   "[--width PIXELS] [--height PIXELS] [--level error|warning|information]";

Pointer::Pointer(std::string_view name, std::int32_t x, std::int32_t y, bool visible)
    : _name{ name }, _x{ x }, _y{ y }, _visible{ visible } { }
auto Pointer::Visible() const -> bool {
  return _visible;
}
auto Pointer::Name() const -> std::string_view {
  return _name;
}
template <typename Value>
  requires std::totally_ordered<Value>
auto Clamp(Value value, Value low, Value high) -> Value {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}
auto Sum(std::array<std::int32_t, 3> const& values) -> std::int32_t {
  auto       total = 0;
  auto const add   = [&total](std::int32_t value) -> void { total += value; };
  for (auto const value : values) add(value);
  return total;
}
auto Names(std::vector<Level> const& levels) -> std::vector<std::string> {
  return levels | std::views::filter([](Level level) -> bool { return level != Level::INFORMATION; })
         | std::views::transform(
             [](Level level) -> std::string { return std::to_string(static_cast<std::int32_t>(level)); })
         | std::ranges::to<std::vector>();
}
auto Largest(std::vector<std::size_t> sizes) -> std::size_t {
  std::ranges::sort(sizes, [](std::size_t left, std::size_t right) -> bool {
    auto const left_even  = left % 2 == 0;
    auto const right_even = right % 2 == 0;
    return left_even == right_even ? left < right : right_even;
  });
  return sizes.empty() ? 0 : sizes.back();
}
auto Name(Phase phase) -> std::string_view {
  switch (phase) {
  case Phase::DOWN: return "down";
  case Phase::MOVE: return "move";
  case Phase::UP:   return "up";
  default:          return { };
  }
}
struct BootStrap {
  using Init = auto (*)() -> bool;
  char const* name;
  char const* description;
  Init        init;
  bool        demand;
};
auto Init() -> bool {
  return true;
}
}
// SDL's C bootstrap table requires this named object with static storage.
extern "C" fixture::BootStrap const RDP_bootstrap = { "rdp", "SDL RDP video driver", fixture::Init, false };
