#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/descriptor.hpp>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace sdl_rdp::headless_client_test::utilities::detail::io {
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Expects;
inline auto ReadText(int descriptor) -> std::string {
  Expects(descriptor >= 0, "input descriptor exists");
  std::string            result;
  std::array<char, 4096> buffer{ };
  for (;;) {
    auto count = read(descriptor, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    Expects(count >= 0, "text read succeeds");
    if (!count) return result;
    result.append(buffer.data(), count);
  }
}
inline auto ReadText(std::filesystem::path const& path) -> std::string {
  Descriptor const file{ open(path.c_str(), O_RDONLY) };
  return ReadText(file.Get());
}
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::io::ReadText;
}
