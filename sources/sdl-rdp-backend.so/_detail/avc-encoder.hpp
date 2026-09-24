#pragma once
#include "avc.hpp"
#include "extent.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>
#include <winpr/wtypes.h>

namespace Backend::Avc {
class Encoder {
public:
                        Encoder();
                        Encoder(Encoder const&)     = delete;
                        Encoder(Encoder&&)          = delete;
                        ~Encoder();
  Encoder&              operator = (Encoder const&) = delete;
  Encoder&              operator = (Encoder&&)      = delete;
  static bool           Available();
  static std::string    UnavailableReason();
  bool                  Open(Extent size, unsigned bitrate, unsigned fps);
  std::span<BYTE const> Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr, std::vector<BYTE>& encoded);
  void                  Close();
  bool                  IsOpen() const;
  bool                  TooSmall() const;
  std::string const&    Error() const;
  EncodingTimes const&  Timing() const;

private:
  EncodingTimes         times;
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace Backend::Avc
