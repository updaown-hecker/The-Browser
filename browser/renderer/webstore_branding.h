#ifndef BROWSER_RENDERER_WEBSTORE_BRANDING_H_
#define BROWSER_RENDERER_WEBSTORE_BRANDING_H_

namespace blink {
class WebLocalFrame;
}
namespace browser {
// Cosmetic Store presentation only; exposes no bindings and never invokes an installer.
void ApplyWebStoreBranding(blink::WebLocalFrame* frame, int isolated_world);
}  // namespace browser
#endif
