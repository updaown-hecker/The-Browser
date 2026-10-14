#include <string>

#include "base/files/file_path.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "content/public/test/browser_test.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/test_extension_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace browser {

using BrowserFinalExtensionTest = extensions::ExtensionApiTest;

// This runs a real MV3 worker in Chromium's extension environment. chrome.test
// is provided by the browser-test harness, not exposed to ordinary websites.
IN_PROC_BROWSER_TEST_F(BrowserFinalExtensionTest,
                       UnpackedMv3StorageWorkerAndDisableEnable) {
  extensions::TestExtensionDir directory;
  directory.WriteManifest(R"({
    "manifest_version": 3,
    "name": "Blynx local MV3 regression",
    "version": "1.0",
    "permissions": ["storage"],
    "background": {"service_worker": "worker.js"}
  })");
  directory.WriteFile(FILE_PATH_LITERAL("worker.js"), R"(
    (async () => {
      const previous = await chrome.storage.local.get('roundtrips');
      const next = (previous.roundtrips || 0) + 1;
      await chrome.storage.local.set({roundtrips: next});
      const saved = await chrome.storage.local.get('roundtrips');
      chrome.test.sendMessage(saved.roundtrips === next ? 'ready' : 'failed');
    })().catch(() => chrome.test.sendMessage('failed'));
  )");

  ExtensionTestMessageListener worker("ready");
  worker.set_failure_message("failed");
  const extensions::Extension* extension =
      LoadExtension(directory.UnpackedPath());
  ASSERT_TRUE(extension);
  const std::string id = extension->id();
  worker.set_extension_id(id);
  ASSERT_TRUE(worker.WaitUntilSatisfied());
  ASSERT_TRUE(extension_registry()->enabled_extensions().Contains(id));

  extension_registrar()->DisableExtension(
      id, {extensions::disable_reason::DISABLE_USER_ACTION});
  EXPECT_FALSE(extension_registry()->enabled_extensions().Contains(id));
  ASSERT_TRUE(extension_registry()->disabled_extensions().Contains(id));

  worker.Reset();
  extension_registrar()->EnableExtension(id);
  EXPECT_TRUE(extension_registry()->enabled_extensions().Contains(id));
  EXPECT_FALSE(extension_registry()->disabled_extensions().Contains(id));
  ASSERT_TRUE(worker.WaitUntilSatisfied());
}

}  // namespace browser
