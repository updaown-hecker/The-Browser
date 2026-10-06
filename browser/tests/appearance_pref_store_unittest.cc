#include "browser/appearance/appearance_pref_store.h"

#include <memory>
#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/values.h"
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
