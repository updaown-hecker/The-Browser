"""Refuse stale generated-string inputs before starting a Chromium build."""
import tempfile
import unittest
from pathlib import Path

from scripts.bootstrap import ToolError
from scripts.integrate import validate_customization_catalog


class CustomizationCatalogTest(unittest.TestCase):
    def test_project_catalog_matches_reviewed_integration(self):
        validate_customization_catalog(Path(__file__).resolve().parents[1])

    def test_new_owned_string_requires_the_patch_copy_to_be_updated(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            catalog = repo / "browser/resources/customization_strings.grdp"
            catalog.parent.mkdir(parents=True)
            catalog.write_text("<grit-part>\n<message>New string</message>\n</grit-part>\n")
            patch = repo / "patches/0002-customization.patch"
            patch.parent.mkdir()
            patch.write_text(
                "diff --git a/chrome/app/browser_final_customization_strings.grdp "
                "b/chrome/app/browser_final_customization_strings.grdp\n"
                "new file mode 100644\n--- /dev/null\n"
                "+++ b/chrome/app/browser_final_customization_strings.grdp\n"
                "@@ -0,0 +1,2 @@\n+<grit-part>\n+</grit-part>\n")
            with self.assertRaisesRegex(ToolError, "differs"):
                validate_customization_catalog(repo)


class AssistantCatalogTest(unittest.TestCase):
    def test_assistant_catalog_must_match_added_patch_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            catalog = repo / "browser/resources/assistant_strings.grdp"
            catalog.parent.mkdir(parents=True)
            catalog.write_text("<grit-part>new</grit-part>\n")
            patch = repo / "patches/0005-blynx-experience.patch"
            patch.parent.mkdir()
            patch.write_text("diff --git a/chrome/app/blynx_assistant_strings.grdp "
                             "b/chrome/app/blynx_assistant_strings.grdp\n"
                             "new file mode 100644\n--- /dev/null\n"
                             "+++ b/chrome/app/blynx_assistant_strings.grdp\n"
                             "@@ -0,0 +1 @@\n+<grit-part>old</grit-part>\n")
            with self.assertRaisesRegex(ToolError, "update both copies"):
                validate_customization_catalog(repo)
