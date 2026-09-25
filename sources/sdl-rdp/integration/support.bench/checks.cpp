#include <sdl-rdp/integration/support.bench/checks.hpp>

#include <sdl-rdp/utilities/narrowed.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::integration::support_bench::detail::checks {
auto Check(bool held, std::string_view text, std::source_location where) -> bool {
  if (!held) Fail(text, where);
  return held;
}
auto Fail(std::string_view text, std::source_location where) -> void {
  ADD_FAILURE_AT(where.file_name(), Backend::Narrowed<int>(where.line())) << text;
}
auto Skip(std::string_view reason) -> void {
  GTEST_SKIP() << reason;
}
}
