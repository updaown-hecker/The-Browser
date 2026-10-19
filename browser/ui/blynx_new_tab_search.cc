#include "browser/ui/blynx_new_tab_search.h"

#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/cpp/resource_request_body.h"

namespace browser {
void SearchFromBlynxNewTab(Profile& profile,
                           content::WebContents& contents,
                           const std::string& query) {
  if (!profile.IsRegularProfile() || profile.IsOffTheRecord() ||
      query.size() > 8000 || !base::IsStringUTF8(query) ||
      base::TrimWhitespaceASCII(query, base::TRIM_ALL).empty()) {
    return;
  }
  auto* service = TemplateURLServiceFactory::GetForProfile(&profile);
  const auto* provider =
      service ? service->GetDefaultSearchProvider() : nullptr;
  if (!provider) {
    return;
  }
  TemplateURLRef::PostContent post_content;
  const GURL url(provider->url_ref().ReplaceSearchTerms(
      TemplateURLRef::SearchTermsArgs(base::UTF8ToUTF16(query)),
      service->search_terms_data(), &post_content));
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
      url.has_password()) {
    return;
  }
  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::CURRENT_TAB,
                                ui::PAGE_TRANSITION_GENERATED, false);
  if (!post_content.second.empty()) {
    params.post_data = base::MakeRefCounted<network::ResourceRequestBody>();
    params.post_data->AppendCopyOfBytes(
        base::as_byte_span(post_content.second));
    params.extra_headers = "Content-Type: " + post_content.first + "\r\n";
  }
  contents.OpenURL(params, /*navigation_handle_callback=*/{});
}
}  // namespace browser
