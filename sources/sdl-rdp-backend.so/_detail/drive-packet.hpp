#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Backend {
class DriveChannel;
class DrivePacket {
public:
  [[noreturn]] void           Invalid(std::string const& cause) const;
  uint64_t                    Get(unsigned count);
  void                        Put(uint64_t value, unsigned count = 4);
  void                        Zero(size_t count);
  void                        Append(std::span<uint8_t const> data);
  void                        Skip(size_t count);
  std::string                 Text(size_t count);
  std::vector<uint8_t>&       Bytes();
  std::vector<uint8_t> const& Bytes() const;
  size_t                      Position() const;
  void                        Seek(size_t offset);
  void                        Origin(std::weak_ptr<DriveChannel> channel);

private:
  std::weak_ptr<DriveChannel> origin;
  std::vector<uint8_t>        bytes;
  size_t                      position{ };
};
std::vector<uint8_t> DrivePath(char const* path);
}
