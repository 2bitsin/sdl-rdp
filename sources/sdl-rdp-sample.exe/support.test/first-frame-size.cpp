#include "support.test/first-frame-size.hpp"

namespace SampleGate {
using utilities::Expects;

FirstFrameSize::FirstFrameSize(Headless::Client& value)
    : client(value), original_connect(value.Instance()->PostConnect) {
  Expects(!active, "no observer is already installed");
  Expects(original_connect, "original connection callback is installed");
  active                         = this;
  client.Instance()->PostConnect = Connect;
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
auto FirstFrameSize::Connect(freerdp* instance) -> BOOL {
  Expects(active, "observer is installed");
  Expects(instance, "FreeRDP instance exists");
  if (!active->original_connect(instance)) return FALSE;
  active->original_paint              = instance->context->update->EndPaint;
  instance->context->update->EndPaint = Paint;
  active->paint_installed             = true;
  return TRUE;
}
auto FirstFrameSize::Paint(rdpContext* context) -> BOOL {
  Expects(active, "observer is installed");
  Expects(context, "callback context exists");
  Expects(context->gdi, "decoded framebuffer exists");
  if (!active->received) {
    active->width    = context->gdi->width;
    active->height   = context->gdi->height;
    active->received = true;
  }
  return active->original_paint ? active->original_paint(context) : TRUE;
}
}
