#ifndef BROWSER_AI_ASSISTANT_PROTOCOL_H_
#define BROWSER_AI_ASSISTANT_PROTOCOL_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "url/gurl.h"

class PrefRegistrySimple;
class PrefService;

namespace browser {
inline constexpr char kAssistantConfigPref[] = "blynx.assistant_config";
base::DictValue DefaultAssistantConfig();
void RegisterAssistantPrefs(PrefRegistrySimple* registry);
bool ValidateAssistantConfig(const base::DictValue& config);
bool CanWriteAssistantConfig(const PrefService& prefs);
bool WriteAssistantConfig(PrefService& prefs, const base::DictValue& config);
bool IsAssistantWebURL(const GURL& url);
// Validates the entire proposal before any action can be offered to the user.
bool ValidateAssistantActions(const base::ListValue& actions);
std::string AssistantSystemPrompt();
// Accept both portable JSON proposals and ordinary conversational replies.
std::optional<base::DictValue> ParseAssistantReply(std::string_view text);
// Parse only bounded Ollama model names from /api/tags. The result is safe to
// use as native selector text, but is not written to preferences by this API.
std::optional<std::vector<std::string>> ParseOllamaModels(
    std::string_view text);
}  // namespace browser
#endif
