#pragma once
#include "avc.hpp"
#include "extent.hpp"

#include <winpr/wtypes.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Backend::Avc {
class Encoder {
public:
              Encoder();
              Encoder(Encoder const&)                                       = delete;
              Encoder(Encoder&&)                                            = delete;
              ~Encoder();
  auto        operator=(Encoder const&)                         -> Encoder& = delete;
  auto        operator=(Encoder&&)                              -> Encoder& = delete;
  static auto Available()                                       -> bool;
  static auto UnavailableReason()                               -> std::string;
  auto        Open(Extent size, unsigned bitrate, unsigned fps) -> bool;
  auto        Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr, std::vector<BYTE>& encoded)
      -> std::span<BYTE const>;
  auto        Close()                                           -> void;
  auto        IsOpen() const                                    -> bool;
  auto        TooSmall() const                                  -> bool;
  auto        Error() const                                     -> std::string const&;
  auto        Timing() const                                    -> EncodingTimes const&;

private:
  EncodingTimes         times;
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace Backend::Avc
