#pragma once
#include <source_location>
#include <string_view>

namespace sdl_rdp::bench::support_bench::detail::checks {
auto Check(bool held, std::string_view text, std::source_location where = std::source_location::current()) -> bool;
auto Fail(std::string_view text, std::source_location where = std::source_location::current())             -> void;
auto Skip(std::string_view reason)                                                                         -> void;
}

namespace sdl_rdp::bench::support_bench {
using detail::checks::Check;
using detail::checks::Fail;
using detail::checks::Skip;
}
