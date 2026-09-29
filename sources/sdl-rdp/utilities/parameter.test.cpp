#include <sdl-rdp/utilities/parameter.hpp>

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace sdl_rdp::utilities::detail::parameter {
namespace {
auto Measured([[maybe_unused]] std::uint16_t unused_width, [[maybe_unused]] std::uint32_t& unused_length,
              [[maybe_unused]] double unused_scale) -> int {
  return 0;
}
auto Unthrowing([[maybe_unused]] char16_t unused_unit, [[maybe_unused]] std::int64_t& unused_count) noexcept -> void { }

class Gauge {
public:
  auto Reset(std::uint8_t level) -> void {
    _level = level;
  }
  auto Read([[maybe_unused]] std::size_t unused_index, [[maybe_unused]] bool unused_fresh) const noexcept
      -> std::int32_t {
    return _level;
  }

private:
  std::int32_t _level{ 0 };
};

constexpr auto Scaled = [](Gauge& gauge, std::int32_t factor) -> std::int32_t { return gauge.Read(0, true) * factor; };

static_assert(std::is_same_v<Parameter<Measured, 0>, std::uint16_t>);
static_assert(std::is_same_v<Parameter<Measured, 1>, std::uint32_t&>);
static_assert(std::is_same_v<Parameters<Measured>, std::tuple<std::uint16_t, std::uint32_t&, double>>);
static_assert(std::is_same_v<Parameter<&Unthrowing, 1>, std::int64_t&>);
static_assert(std::is_same_v<Parameters<&Gauge::Reset>, std::tuple<std::uint8_t>>);
static_assert(std::is_same_v<Parameters<&Gauge::Read>, std::tuple<std::size_t, bool>>);
static_assert(std::is_same_v<Parameter<Scaled, 0>, Gauge&>);
static_assert(std::is_same_v<Parameters<Scaled>, std::tuple<Gauge&, std::int32_t>>);
}
}
