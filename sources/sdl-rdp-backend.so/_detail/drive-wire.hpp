#pragma once
#include "contract.hpp"
#include "transcode.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace Backend {
class DriveChannel;
struct MalformedResponse : std::runtime_error {
public:
  MalformedResponse(std::string const& cause, std::weak_ptr<DriveChannel> channel)
      : std::runtime_error(cause), origin(std::move(channel)) {}
  std::weak_ptr<DriveChannel> origin;
};
struct DrivePacket {
public:
  [[noreturn]] void Invalid(std::string const& cause) const { throw MalformedResponse(cause, origin); }
  uint64_t Get(unsigned count) {
    utilities::Expects(count <= 8, "integer fits uint64");
    utilities::Expects(position <= bytes.size(), "packet cursor is bounded");
    if (count > bytes.size() - position) Invalid("Truncated drive response.");
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
      value |= uint64_t(bytes[position++]) << (i * 8);
    return value;
  }
  void Put(uint64_t value, unsigned count = 4) {
    utilities::Expects(count <= 8, "integer fits uint64");
    for (unsigned i = 0; i < count; ++i)
      bytes.push_back(uint8_t(value >> (i * 8)));
  }
  void Zero(size_t count) { bytes.resize(bytes.size() + count); }
  void Append(std::span<uint8_t const> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
  void Skip(size_t count) {
    utilities::Expects(position <= bytes.size(), "packet cursor is bounded");
    if (count > bytes.size() - position) Invalid("Truncated drive response.");
    position += count;
  }
  std::string Text(size_t count) {
    utilities::Expects(position <= bytes.size(), "packet cursor is bounded");
    if (count > bytes.size() - position) Invalid("Truncated drive name.");
    try {
      auto result = TranscodeRange<std::string>(
          std::as_bytes(std::span(bytes).subspan(position, count)),
          { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little }, {});
      position += count;
      return result;
    } catch (std::exception const& error) {
      Invalid(error.what());
    }
  }
  auto& Bytes() { return bytes; }
  auto const& Bytes() const { return bytes; }
  size_t Position() const { return position; }
  void Seek(size_t offset) {
    utilities::Expects(offset <= bytes.size(), "packet cursor is bounded");
    position = offset;
  }
  void Origin(std::weak_ptr<DriveChannel> channel) { origin = std::move(channel); }

private:
  std::weak_ptr<DriveChannel> origin;
  std::vector<uint8_t>        bytes;
  size_t                      position = 0;
};
inline std::vector<uint8_t> DrivePath(char const* path) {
  if (!path) throw std::runtime_error("Drive path is null.");
  std::string text(path);
  if (text.empty() || text.front() != '/') text.insert(text.begin(), '/');
  auto encoded = TranscodeRange<std::vector<uint8_t>>(
      std::as_bytes(std::span(text)), {},
      { .encoding = oxbox::utilities::Encoding::UTF16, .order = std::endian::little },
      [](char32_t point) { return point == U'/' ? U'\\' : point; });
  encoded.resize(encoded.size() + 2);
  return encoded;
}
}
