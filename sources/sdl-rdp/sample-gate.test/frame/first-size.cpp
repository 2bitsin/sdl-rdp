#include <sdl-rdp/sample-gate.test/frame/first-size.hpp>

namespace sdl_rdp::sample_gate_test::frame::detail::first_size {
using sdl_rdp::utilities::Expects;

FirstFrameSize::FirstFrameSize(Client& value) : client(value), original_connect(value.Instance()->PostConnect) {
  Expects(!active, "no observer is already installed");
  Expects(original_connect, "original connection callback is installed");
  active = this;
  // abi: pConnectCallback, BOOL is int
  client.Instance()->PostConnect = [](freerdp* instance) -> int {
    Expects(active, "observer is installed");
    Expects(instance, "FreeRDP instance exists");
    return active->Connect(*instance);
  };
}
FirstFrameSize::~FirstFrameSize() {
  client.Instance()->PostConnect = original_connect;
  if (paint_installed) client.Instance()->context->update->EndPaint = original_paint;
  active = nullptr;
}
auto FirstFrameSize::Received() const -> bool {
  return received;
}
auto FirstFrameSize::Width() const -> int {
  return width;
}
auto FirstFrameSize::Height() const -> int {
  return height;
}
auto FirstFrameSize::Connect(freerdp& instance) -> bool {
  if (!original_connect(&instance)) return false;
  original_paint = instance.context->update->EndPaint;
  // abi: pEndPaint, BOOL is int
  instance.context->update->EndPaint = [](rdpContext* context) -> int {
    Expects(active, "observer is installed");
    Expects(context, "callback context exists");
    return active->Paint(*context);
  };
  paint_installed                    = true;
  return true;
}
auto FirstFrameSize::Paint(rdpContext& context) -> bool {
  Expects(context.gdi, "decoded framebuffer exists");
  if (!received) {
    width    = context.gdi->width;
    height   = context.gdi->height;
    received = true;
  }
  return original_paint ? original_paint(&context) : true;
}
}
