#pragma once
#include "contract.hpp"
#include "sdl-rdp-backend.h"

#include <chrono>
#include <freerdp/channels/rdpgfx.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Backend::Avc {
struct IntraRefresh {
  unsigned period;
  unsigned count;
};
IntraRefresh IntraRefreshFor(unsigned fps);
unsigned     Bitrate(unsigned width, unsigned height, unsigned kbps = 0);
unsigned     Aligned(unsigned dimension);
void         ReplicateEdges(std::span<BYTE> pixels, unsigned width, unsigned height);
struct Regions {
public:
  void Add(sdlrdp_rect area);
  std::size_t Bytes() const {
    utilities::Expects(rects.size() == quality.size(), "every region has quantization metadata");
    return 4 + (10 * rects.size());
  }
  auto& Rects() { return rects; }
  auto& Quality() { return quality; }
  sdlrdp_rect Bounds() const { return bounds; }
  void Clear() {
    rects.clear();
    quality.clear();
  }

private:
  std::vector<RECTANGLE_16>              rects;
  std::vector<RDPGFX_H264_QUANT_QUALITY> quality;
  sdlrdp_rect                            bounds { };
};
struct EncodingTimes {
  std::chrono::nanoseconds convert{ };
  std::chrono::nanoseconds upload { };
  std::chrono::nanoseconds encode { };
};
class Encoder {
public:
                        Encoder();
                        Encoder(Encoder const&) = delete;
                        Encoder(Encoder&&) = delete;
                        ~Encoder();
  Encoder&              operator =(Encoder const&) = delete;
  Encoder&              operator =(Encoder&&) = delete;
  static bool           Available();
  static std::string    UnavailableReason();
  bool                  Open(unsigned width, unsigned height, unsigned bitrate, unsigned fps);
  std::span<BYTE const> Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr, std::vector<BYTE>& encoded);
  void                  Close();
  bool                  IsOpen() const;
  bool                  TooSmall() const;
  std::string const&    Error() const;
  EncodingTimes const& Timing() const { return times; }

private:
  EncodingTimes times;
  struct                Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace Backend::Avc
