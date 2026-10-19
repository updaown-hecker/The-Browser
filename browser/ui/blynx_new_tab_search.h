#ifndef BROWSER_UI_BLYNX_NEW_TAB_SEARCH_H_
#define BROWSER_UI_BLYNX_NEW_TAB_SEARCH_H_
#include <string>
class Profile;
namespace content {
class WebContents;
}
namespace browser {
// Trusted new-tab query only: provider URL, encoding and optional POST body are
// derived from Chromium's current default-search service, never from the page.
void SearchFromBlynxNewTab(Profile& profile,
                           content::WebContents& contents,
                           const std::string& query);
}  // namespace browser
#endif
