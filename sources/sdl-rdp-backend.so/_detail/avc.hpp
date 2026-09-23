#pragma once
#include "contract.hpp"
#include "sdl-rdp-backend.h"
#include <freerdp/channels/rdpgfx.h>
#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Backend::Avc {
unsigned Bitrate(unsigned width, unsigned height, unsigned kbps = 0);
unsigned Aligned(unsigned dimension);
void Pad(std::span<BYTE const> pixels, unsigned stride, unsigned width, unsigned height, std::vector<BYTE>& padded);
struct Regions {
  std::vector<RECTANGLE_16> rects;
  std::vector<RDPGFX_H264_QUANT_QUALITY> quality;
  sdlrdp_rect bounds{};
  void Add(sdlrdp_rect area);
  std::size_t Bytes() const {
    utilities::Expects(rects.size() == quality.size(), "every region has quantization metadata");
    return 4 + 10 * rects.size();
  }
};
class Encoder {
public:
  std::chrono::nanoseconds convert_time{}, upload_time{}, encode_time{};
  Encoder();
  ~Encoder();
  Encoder(Encoder const&) = delete;
  Encoder& operator=(Encoder const&) = delete;
  static bool Available();
  static std::string UnavailableReason();
  bool Open(unsigned width, unsigned height, unsigned bitrate, unsigned fps);
  std::span<BYTE const> Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr);
  void Close();
  bool IsOpen() const;
  bool TooSmall() const;
  std::string const& Error() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
}
