#include "browser/renderer/webstore_branding.h"

#include <string>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "extensions/strings/grit/extensions_strings.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace browser {
namespace {
// Preserve installation elements, listeners, permissions and disabled state.
// The observer handles Store SPA rendering, with at most one scan per frame.
constexpr char kBrandStoreButtons[] = R"JS((function(label) {
  'use strict';
  if (location.origin !== 'https://chromewebstore.google.com' ||
      window !== window.top || !document.documentElement) return;
  const original = /^Add to (?:Google Chrome|Chrome|Chromium)$/;
  const matches = text => original.test(text.trim());
  const bannerText = 'Switch to Chrome to install extensions and themes';
  function hidePromotion(node) {
    node.hidden = true;
    node.inert = true;
    node.style.setProperty('display', 'none', 'important');
  }
  function hideChromePromotions() {
    for (const banner of document.querySelectorAll('.gSrP5d')) {
      const message = banner.querySelector('.UWpb5');
      const install = banner.querySelector('button[aria-label]');
      if (message?.textContent.trim() === bannerText &&
          install?.getAttribute('aria-label') === bannerText + ' Install Chrome') {
        hidePromotion(banner);
      }
    }
    for (const dialog of document.querySelectorAll('[role="dialog"]')) {
      if (dialog.querySelector('#promo-header')?.textContent.trim() !==
          'Switch to Chrome?' ||
          dialog.querySelector('.XTn9Fc')?.textContent.trim() !==
          'Google recommends using Chrome when using extensions and themes.') continue;
      // Use the site's own dismissal to release its focus/scrim state, then
      // hide any retained promo node. No installation control is clicked.
      if (!dialog.hidden) {
        const dismiss = dialog.querySelector('button[jsname="gQ2Xie"]');
        if (dismiss?.textContent.trim() === 'No thanks') dismiss.click();
      }
      hidePromotion(dialog);
    }
  }
  let queued = false;
  function update() {
    queued = false;
    hideChromePromotions();
    if (!location.pathname.startsWith('/detail/')) return;
    for (const button of document.querySelectorAll('button, [role="button"]')) {
      const visibleMatch = matches(button.textContent);
      const nodes = document.createTreeWalker(button, NodeFilter.SHOW_TEXT);
      for (let node = nodes.nextNode(); node; node = nodes.nextNode()) {
        if (visibleMatch && matches(node.nodeValue)) {
          node.nodeValue = node.nodeValue.replace(node.nodeValue.trim(), label);
        }
      }
      const accessibleName = button.getAttribute('aria-label');
      if (accessibleName && matches(accessibleName)) {
        button.setAttribute('aria-label', label);
      }
    }
  }
  const observer = new MutationObserver(() => {
    if (!queued) {
      queued = true;
      requestAnimationFrame(update);
    }
  });
  observer.observe(document.documentElement, {
    childList: true, characterData: true, subtree: true,
    attributes: true, attributeFilter: ['aria-label']
  });
  update();
}))JS";
}  // namespace

void ApplyWebStoreBranding(blink::WebLocalFrame* frame, int isolated_world) {
  if (!frame || frame->Parent() || frame->IsInFencedFrameTree()) {
    return;
  }
  const GURL url(frame->GetDocument().Url());
  if (!url.SchemeIs("https") || url.host() != "chromewebstore.google.com" ||
      url.EffectiveIntPort() != 443 || url.has_username() ||
      url.has_password()) {
    return;
  }
  const auto label = l10n_util::GetStringUTF8(
      IDS_EXTENSION_INSTALL_PROMPT_ACCEPT_BUTTON_EXTENSION);
  const std::string script = std::string(kBrandStoreButtons) + "(" +
                             base::WriteJson(base::Value(label)).value() + ");";
  frame->ExecuteScriptInIsolatedWorld(
      isolated_world,
      blink::WebScriptSource(blink::WebString::FromUtf8(script)),
      blink::BackForwardCacheAware::kAllow);
}
}  // namespace browser
