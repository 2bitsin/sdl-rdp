#pragma once

#include <memory>
#include <openssl/bio.h>
#include <winpr/handle.h>
#include <winpr/wtsapi.h>

namespace Backend {
template <auto RELEASE> struct Releases {
public:
  template <typename VTy> auto operator()(VTy* what) const -> void { (void)RELEASE(what); }
};

using EventHandle    = std::unique_ptr<void, Releases<CloseHandle>>;
using Bio            = std::unique_ptr<BIO, Releases<BIO_free>>;
using VirtualChannel = std::unique_ptr<void, Releases<WTSVirtualChannelClose>>;
}
