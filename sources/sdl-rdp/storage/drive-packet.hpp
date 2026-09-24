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
  [[noreturn]] auto Invalid(std::string const& cause) const     -> void;
  auto              Get(unsigned count)                         -> uint64_t;
  auto              Put(uint64_t value, unsigned count = 4)     -> void;
  auto              Zero(size_t count)                          -> void;
  auto              Append(std::span<uint8_t const> data)       -> void;
  auto              Skip(size_t count)                          -> void;
  auto              Text(size_t count)                          -> std::string;
  auto              Bytes()                                     -> std::vector<uint8_t>&;
  auto              Bytes() const                               -> std::vector<uint8_t> const&;
  auto              Position() const                            -> size_t;
  auto              Seek(size_t offset)                         -> void;
  auto              Origin(std::weak_ptr<DriveChannel> channel) -> void;

private:
  std::weak_ptr<DriveChannel> origin;
  std::vector<uint8_t>        bytes;
  size_t                      position{ };
};
auto DrivePath(char const* path) -> std::vector<uint8_t>;
}
