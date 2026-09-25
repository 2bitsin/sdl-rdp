#pragma once
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sdl_rdp::video::avc::detail::encoder {
using sdl_rdp::utilities::Extent;

class Encoder {
public:
              Encoder();
              Encoder(Encoder const&)                                                 = delete;
              Encoder(Encoder&&)                                                      = delete;
              ~Encoder();
  auto        operator=(Encoder const&)                                   -> Encoder& = delete;
  auto        operator=(Encoder&&)                                        -> Encoder& = delete;
  static auto Available()                                                 -> bool;
  static auto UnavailableReason()                                         -> std::string;
  auto        Open(Extent size, std::uint32_t bitrate, std::uint32_t fps) -> bool;
  auto Encode(std::span<std::uint8_t const> bgrx, std::uint32_t stride, bool force_idr, std::vector<std::byte>& encoded)
      -> std::span<std::byte const>;
  auto        Close()                                                     -> void;
  auto        IsOpen() const                                              -> bool;
  auto        TooSmall() const                                            -> bool;
  auto        Error() const                                               -> std::string const&;
  auto        Timing() const                                              -> EncodingTimes const&;

private:
  EncodingTimes         times;
  struct Impl;
  std::unique_ptr<Impl> impl;
};
}

namespace sdl_rdp::video::avc {
using detail::encoder::Encoder;
}
