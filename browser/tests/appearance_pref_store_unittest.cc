#include "browser/appearance/appearance_pref_store.h"

#include <memory>
#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "browser/ai/assistant_protocol.h"
#include "browser/appearance/search_appearance.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace browser {
namespace {

class AppearancePrefStoreTest : public testing::Test {
 protected:
  AppearancePrefStoreTest() {
    RegisterAppearanceProfilePrefs(prefs_.registry());
  }

  AppearancePrefStore Store() {
    return AppearancePrefStore(prefs_, AppearancePrefAccess::kWritableProfile);
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple prefs_;
};

TEST_F(AppearancePrefStoreTest, UnconfiguredReadDoesNotWriteDefaults) {
  const auto read = Store().Read();
  EXPECT_EQ(AppearancePrefState::kUnconfigured, read.state);
  EXPECT_EQ(AppearancePreset::Default(), read.preset);
  EXPECT_FALSE(prefs_.GetUserPrefValue(kAppearanceJsonPref));
}

TEST_F(AppearancePrefStoreTest, SavedPresetRoundTripsThroughPrefService) {
  for (const auto& preset : BuiltInAppearancePresets()) {
    ASSERT_EQ(AppearancePrefWriteResult::kSaved, Store().Write(preset));
    const auto loaded = Store().Read();
    EXPECT_EQ(AppearancePrefState::kSaved, loaded.state);
    EXPECT_EQ(preset, loaded.preset);
    EXPECT_LE(prefs_.GetString(kAppearanceJsonPref).size(),
              AppearancePreset::kMaximumJsonBytes);
  }
}

TEST_F(AppearancePrefStoreTest, MalformedAndOversizedInputIsNonMutating) {
  for (const auto& input :
       {std::string("not json"), std::string("{}"),
        std::string(AppearancePreset::kMaximumJsonBytes + 1, 'x')}) {
    prefs_.SetString(kAppearanceJsonPref, input);
    const auto loaded = Store().Read();
    EXPECT_EQ(AppearancePrefState::kInvalid, loaded.state);
    EXPECT_EQ(AppearancePreset::Default(), loaded.preset);
    EXPECT_EQ(input, prefs_.GetString(kAppearanceJsonPref));
  }
  EXPECT_EQ(AppearancePrefWriteResult::kSaved,
            Store().Write(AppearancePreset::Dark()));
}

TEST_F(AppearancePrefStoreTest, FutureAndUnknownVersionsCannotBeOverwritten) {
  for (const auto& input : {
           std::string("{\"schema_version\":2}"),
           std::string("{\"new_field\":{\"value\":true},\"schema_version\":2}"),
           std::string("{\"schema_version\":0}"),
       }) {
    prefs_.SetString(kAppearanceJsonPref, input);
    EXPECT_EQ(AppearancePrefState::kUnsupported, Store().Read().state);
    EXPECT_EQ(AppearancePrefWriteResult::kUnsupportedStoredVersion,
              Store().Write(AppearancePreset::Default()));
    EXPECT_EQ(input, prefs_.GetString(kAppearanceJsonPref));
  }
}

TEST_F(AppearancePrefStoreTest, InvalidPresetDoesNotReplaceSavedConfiguration) {
  ASSERT_EQ(AppearancePrefWriteResult::kSaved,
            Store().Write(AppearancePreset::Dark()));
  const std::string before = prefs_.GetString(kAppearanceJsonPref);
  auto invalid = AppearancePreset::Compact();
  invalid.accent_color = "red";
  EXPECT_EQ(AppearancePrefWriteResult::kInvalidPreset, Store().Write(invalid));
  EXPECT_EQ(before, prefs_.GetString(kAppearanceJsonPref));
}

TEST_F(AppearancePrefStoreTest,
       ManagedAndExtensionControlledPrefsRefuseWrites) {
  const auto managed = *ExportAppearancePreset(AppearancePreset::Dark());
  prefs_.SetManagedPref(kAppearanceJsonPref, base::Value(managed));
  EXPECT_EQ(AppearancePreset::Dark(), Store().Read().preset);
  EXPECT_EQ(AppearancePrefWriteResult::kManaged,
            Store().Write(AppearancePreset::Compact()));
  EXPECT_FALSE(prefs_.GetUserPrefValue(kAppearanceJsonPref));
  prefs_.RemoveManagedPref(kAppearanceJsonPref);
  prefs_.SetExtensionPref(kAppearanceJsonPref, base::Value(managed));
  EXPECT_EQ(AppearancePrefWriteResult::kManaged,
            Store().Write(AppearancePreset::Default()));
  EXPECT_FALSE(prefs_.GetUserPrefValue(kAppearanceJsonPref));
}

TEST_F(AppearancePrefStoreTest, ReadOnlyAccessDoesNotWrite) {
  AppearancePrefStore read_only(prefs_, AppearancePrefAccess::kReadOnlyProfile);
  EXPECT_EQ(AppearancePrefWriteResult::kReadOnlyProfile,
            read_only.Write(AppearancePreset::Dark()));
  EXPECT_FALSE(prefs_.GetUserPrefValue(kAppearanceJsonPref));
}

TEST_F(AppearancePrefStoreTest, MissingRegistrationIsSafe) {
  TestingPrefServiceSimple unregistered;
  AppearancePrefStore store(unregistered,
                            AppearancePrefAccess::kWritableProfile);
  EXPECT_EQ(AppearancePrefState::kInvalid, store.Read().state);
  EXPECT_EQ(AppearancePrefWriteResult::kUnavailable,
            store.Write(AppearancePreset::Default()));
}

TEST_F(AppearancePrefStoreTest, FileBackedPrefServiceSurvivesRecreation) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto path = directory.GetPath().AppendASCII("Preferences");
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  RegisterAppearanceProfilePrefs(registry.get());
  auto create = [&]() {
    PrefServiceFactory factory;
    factory.SetUserPrefsFile(
        path, base::SequencedTaskRunner::GetCurrentDefault().get());
    return factory.Create(registry);
  };
  {
    auto prefs = create();
    AppearancePrefStore store(*prefs, AppearancePrefAccess::kWritableProfile);
    ASSERT_EQ(AppearancePrefWriteResult::kSaved,
              store.Write(AppearancePreset::Dark()));
    base::RunLoop committed;
    prefs->CommitPendingWrite(committed.QuitClosure());
    committed.Run();
  }
  auto reopened = create();
  AppearancePrefStore store(*reopened, AppearancePrefAccess::kWritableProfile);
  EXPECT_EQ(AppearancePrefState::kSaved, store.Read().state);
  EXPECT_EQ(AppearancePreset::Dark(), store.Read().preset);
}

}  // namespace
}  // namespace browser

namespace browser {
TEST(SearchAppearanceTest, DefaultsAndRoundTrip) {
  TestingPrefServiceSimple prefs;
  RegisterAppearanceProfilePrefs(prefs.registry());
  EXPECT_TRUE(ValidateSearchAppearance(DefaultSearchAppearance()));
  EXPECT_FALSE(prefs.HasPrefPath(kSearchAppearancePref));
  auto value = DefaultSearchAppearance();
  value.Set("outer_style", "md");
  value.Set("hover_style", "sm");
  value.Set("palette", "custom");
  value.Set("theme", "light");
  value.Set("strength", 1);
  value.Set("active", false);
  value.Set("custom_colors", true);
  value.FindDict("colors")->Set("input_text", "#123abc");
  EXPECT_TRUE(WriteSearchAppearance(prefs, value));
  EXPECT_EQ(LoadSearchAppearance(prefs), value);
}
TEST(SearchAppearanceTest, RejectsInvalidOrUnknownInput) {
  auto value = DefaultSearchAppearance();
  value.Set("strength", 1.01);
  EXPECT_FALSE(ValidateSearchAppearance(value));
  value = DefaultSearchAppearance();
  value.Set("outer_style", "unknown");
  EXPECT_FALSE(ValidateSearchAppearance(value));
  value = DefaultSearchAppearance();
  value.FindDict("colors")->Set("surface", "url(secret)");
  EXPECT_FALSE(ValidateSearchAppearance(value));
  value = DefaultSearchAppearance();
  value.Set("active", "yes");
  EXPECT_FALSE(ValidateSearchAppearance(value));
  value = DefaultSearchAppearance();
  value.Set("extra", 1);
  EXPECT_FALSE(ValidateSearchAppearance(value));
}
TEST(SearchAppearanceTest, ManagedAndFutureVersionsArePreserved) {
  TestingPrefServiceSimple prefs;
  RegisterAppearanceProfilePrefs(prefs.registry());
  auto future = DefaultSearchAppearance();
  future.Set("version", 2);
  prefs.SetDict(kSearchAppearancePref, future.Clone());
  EXPECT_FALSE(WriteSearchAppearance(prefs, DefaultSearchAppearance()));
  EXPECT_EQ(prefs.GetDict(kSearchAppearancePref), future);
  prefs.RemoveUserPref(kSearchAppearancePref);
  prefs.SetManagedPref(kSearchAppearancePref,
                       base::Value(DefaultSearchAppearance()));
  EXPECT_FALSE(WriteSearchAppearance(prefs, DefaultSearchAppearance()));
}

TEST(AssistantProtocolTest, ConfigNeverIncludesKeyAndRequiresSecureEndpoint) {
  auto config = DefaultAssistantConfig();
  EXPECT_TRUE(ValidateAssistantConfig(config));
  EXPECT_FALSE(config.FindBool("enabled").value());
  config.Set("endpoint", "https://remote.example/api/chat");
  EXPECT_FALSE(ValidateAssistantConfig(config));
  config.Set("provider", "api");
  EXPECT_TRUE(ValidateAssistantConfig(config));
  for (const char* endpoint :
       {"http://remote.example/chat", "https://user:secret@example.com/chat",
        "file:///tmp/chat", "https://example.com/chat?key=secret",
        "https://example.com/chat#secret"}) {
    config.Set("endpoint", endpoint);
    EXPECT_FALSE(ValidateAssistantConfig(config));
  }
  config.Set("endpoint", "http://127.0.0.1:1234/v1/chat/completions");
  EXPECT_TRUE(ValidateAssistantConfig(config));
  config.Set("api_key", "never persist this");
  EXPECT_FALSE(ValidateAssistantConfig(config));
}
TEST(AssistantProtocolTest, ProposalsAreAtomicAndBounded) {
  auto reply = ParseAssistantReply(
      R"json({"message":"Review this","actions":[{"type":"open_tab","url":"https://example.com"},{"type":"set_theme","theme":"dark"}]})json");
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply->FindList("actions")->size(), 2u);
  for (
      const char* bad :
      {R"json({"message":"x","actions":[{"type":"open_tab","url":"javascript:alert(1)"}]})json",
       R"json({"message":"x","actions":[{"type":"open_tab","url":"chrome://settings"}]})json",
       R"json({"message":"x","actions":[{"type":"execute_script","code":"evil()"}]})json",
       R"json({"message":"x","actions":[{"type":"close_tab","tab":0}]})json",
       R"json({"message":"x","actions":[{"type":"set_vertical_tabs","enabled":"yes"}]})json",
       R"json({"message":"x","actions":[{"type":"set_theme","theme":"dark","extra":true}]})json",
       R"json({"message":"x","actions":[{"type":"close_tab","tab":1},{"type":"reload","tab":1}]})json"}) {
    EXPECT_FALSE(ParseAssistantReply(bad));
  }
  base::ListValue actions;
  for (int i = 0; i < 6; ++i) {
    actions.Append(base::DictValue()
                       .Set("type", "open_tab")
                       .Set("url", "https://example.com"));
  }
  EXPECT_FALSE(ValidateAssistantActions(actions));
}
TEST(AssistantProtocolTest, ChatAndFencedJSONRemainPlainText) {
  auto chat = ParseAssistantReply("<script>never execute this</script>");
  ASSERT_TRUE(chat);
  EXPECT_TRUE(chat->FindList("actions")->empty());
  auto fenced = ParseAssistantReply(
      "```json\n{\"message\":\"Hello\",\"actions\":[]}\n```");
  ASSERT_TRUE(fenced);
  EXPECT_EQ(*fenced->FindString("message"), "Hello");
  EXPECT_FALSE(ParseAssistantReply(std::string(65537, 'x')));
}
TEST(AssistantProtocolTest, OllamaModelsAreBoundedAndDeduplicated) {
  auto models = ParseOllamaModels(
      R"json({"models":[{"name":"qwen2.5:0.5b"},{"name":"llama3.2:3b"}]})json");
  ASSERT_TRUE(models);
  ASSERT_EQ(models->size(), 2u);
  EXPECT_EQ((*models)[0], "qwen2.5:0.5b");
  EXPECT_EQ((*models)[1], "llama3.2:3b");
  for (
      const auto* invalid :
      {R"json({"models":[{"name":"qwen2.5:0.5b"},{"name":"qwen2.5:0.5b"}]})json",
       R"json({"models":[{"name":""}]})json",
       R"json({"models":[{"model":"missing-name"}]})json",
       R"json({"models":["not-a-model"]})json",
       R"json({"models":{"name":"not-a-list"}})json"}) {
    EXPECT_FALSE(ParseOllamaModels(invalid));
  }
  EXPECT_FALSE(ParseOllamaModels(std::string(512 * 1024 + 1, 'x')));
}
TEST(AssistantProtocolTest, ManagedAndFutureConfigsArePreserved) {
  TestingPrefServiceSimple prefs;
  RegisterAssistantPrefs(prefs.registry());
  auto config = DefaultAssistantConfig();
  config.Set("enabled", true);
  ASSERT_TRUE(WriteAssistantConfig(prefs, config));
  const auto saved = prefs.GetDict(kAssistantConfigPref).Clone();
  auto invalid = config.Clone();
  invalid.Set("key", "secret");
  EXPECT_FALSE(WriteAssistantConfig(prefs, invalid));
  EXPECT_EQ(prefs.GetDict(kAssistantConfigPref), saved);
  config.Set("version", 2);
  prefs.SetDict(kAssistantConfigPref, config.Clone());
  EXPECT_FALSE(WriteAssistantConfig(prefs, DefaultAssistantConfig()));
  EXPECT_EQ(prefs.GetDict(kAssistantConfigPref), config);
  prefs.RemoveUserPref(kAssistantConfigPref);
  prefs.SetManagedPref(kAssistantConfigPref,
                       base::Value(DefaultAssistantConfig()));
  EXPECT_FALSE(CanWriteAssistantConfig(prefs));
  EXPECT_FALSE(WriteAssistantConfig(prefs, saved));
}
}  // namespace browser
