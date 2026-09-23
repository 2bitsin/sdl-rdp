#include "_detail/refresh.hpp"
#include <linux/tcp.h>
#include <linux/sockios.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

namespace Backend {
WireSample SampleWire(int descriptor)
{
  utilities::Expects(descriptor >= 0, "peer socket is open");
  tcp_info info{};
  socklen_t length = sizeof(info);
  int outq = 0;
  if (getsockopt(descriptor, IPPROTO_TCP, TCP_INFO, &info, &length) != 0 ||
      ioctl(descriptor, SIOCOUTQ, &outq) != 0) return {};
  if (length < offsetof(tcp_info, tcpi_delivery_rate) + sizeof(info.tcpi_delivery_rate)) return {};
  return {true, unsigned(outq), info.tcpi_notsent_bytes, info.tcpi_unacked,
    info.tcpi_rtt, info.tcpi_snd_mss, info.tcpi_delivery_rate};
}
}
