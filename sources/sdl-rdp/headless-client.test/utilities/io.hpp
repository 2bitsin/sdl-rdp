#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/descriptor.hpp>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace Headless {
using Backend::Descriptor;
inline auto ReadText(int descriptor) -> std::string {
  utilities::Expects(descriptor >= 0, "input descriptor exists");
  std::string            result;
  std::array<char, 4096> buffer{ };
  for (;;) {
    auto count = read(descriptor, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    utilities::Expects(count >= 0, "text read succeeds");
    if (!count) return result;
    result.append(buffer.data(), count);
  }
}
inline auto ReadText(char const* path) -> std::string {
  utilities::Expects(path != nullptr, "input path exists");
  Descriptor const file{ open(path, O_RDONLY) };
  return ReadText(file.Get());
}
}
