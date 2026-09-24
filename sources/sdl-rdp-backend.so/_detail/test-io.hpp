#pragma once
#include "contract.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace Headless {
struct Descriptor {
public:
           Descriptor(Descriptor const&) = delete;
           Descriptor(Descriptor&&)      = delete;
  explicit Descriptor(int descriptor)    : value{ descriptor } { }
           ~Descriptor()                 {
    if (value >= 0) close(value);
  }
  Descriptor& operator = (Descriptor const&) = delete;
  Descriptor& operator = (Descriptor&&)      = delete;
  int         Get() const                    { return value; }

private:
  int value;
};
inline std::string ReadText(int descriptor) {
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
inline std::string ReadText(char const* path) {
  utilities::Expects(path != nullptr, "input path exists");
  Descriptor const file{ open(path, O_RDONLY) };
  return ReadText(file.Get());
}
}
