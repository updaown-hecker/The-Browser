#include "browser/ui/internal_page_alias.h"

#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"

namespace browser {
InternalPageAlias::InternalPageAlias(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<InternalPageAlias>(*contents) {}

void InternalPageAlias::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (!navigation->HasCommitted() || !navigation->IsInPrimaryMainFrame() ||
      navigation->IsErrorPage()) {
    return;
  }
  auto* entry = web_contents()->GetController().GetLastCommittedEntry();
  if (!entry || !entry->GetURL().SchemeIs(content::kChromeUIScheme) ||
      !entry->GetVirtualURL().SchemeIs(content::kChromeUIScheme)) {
    return;
  }
  GURL::Replacements replacements;
  replacements.SetSchemeStr("blynx");
  entry->SetVirtualURL(entry->GetVirtualURL().ReplaceComponents(replacements));
  web_contents()->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_URL);
}
WEB_CONTENTS_USER_DATA_KEY_IMPL(InternalPageAlias);
}  // namespace browser
