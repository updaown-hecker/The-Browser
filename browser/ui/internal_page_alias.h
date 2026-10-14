#ifndef BROWSER_UI_INTERNAL_PAGE_ALIAS_H_
#define BROWSER_UI_INTERNAL_PAGE_ALIAS_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace browser {
// Only the displayed navigation entry uses the alias. Committed documents,
// origins, WebUI bindings and resource requests remain upstream chrome://.
class InternalPageAlias
    : public content::WebContentsObserver,
      public content::WebContentsUserData<InternalPageAlias> {
 public:
  ~InternalPageAlias() override = default;

 private:
  explicit InternalPageAlias(content::WebContents* contents);
  friend class content::WebContentsUserData<InternalPageAlias>;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace browser
#endif
