"""An interrupted build cannot produce a stale passing runtime smoke report."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.bootstrap import ToolError
from scripts import runtime_smoke


class RuntimeSmokeReadinessTest(unittest.TestCase):
    def test_inconsistent_integration_or_build_refuses_launch_and_clears_old_pass(self):
        for failing_guard in ("_require_integration", "_require_completed_browser_build"):
            with self.subTest(guard=failing_guard), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                output = root / ".local/runtime-smoke"
                output.mkdir(parents=True)
                report_path = output / "results.json"
                report_path.write_text('{"passed":true,"checks":["old pass"]}')
                with mock.patch.object(runtime_smoke, "__file__", str(root / "scripts/runtime_smoke.py")), \
                     mock.patch.object(runtime_smoke, "_require_integration") as integration, \
                     mock.patch.object(runtime_smoke, "_require_completed_browser_build") as completed, \
                     mock.patch.object(runtime_smoke.subprocess, "Popen") as launch:
                    (integration if failing_guard == "_require_integration" else completed).side_effect = ToolError("inconsistent build")
                    with self.assertRaisesRegex(ToolError, "inconsistent"):
                        runtime_smoke.main()
                    launch.assert_not_called()
                report = json.loads(report_path.read_text())
                self.assertFalse(report["passed"])
                self.assertEqual(report["checks"], [])
                self.assertEqual(report["error"], "inconsistent build")
