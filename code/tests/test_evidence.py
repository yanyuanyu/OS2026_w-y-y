"""Regression for orchestration state only; allocator tests use real C code."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("verify_c", Path(__file__).resolve().parents[1] / "tools/verify_c.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class EvidenceTests(unittest.TestCase):
    def test_failed_rerun_invalidates_old_pass(self):
        with tempfile.TemporaryDirectory(prefix="lab2-evidence-") as directory:
            out = Path(directory)
            summary = out / "summary.json"
            summary.write_text(json.dumps({"status": "PASS", "run_id": "OLD"}))
            with patch.object(verify, "OUT", out), \
                    patch.object(verify.subprocess, "check_output", return_value="test environment\n"), \
                    patch.object(verify.subprocess, "run"), \
                    patch.object(verify, "run_case", side_effect=RuntimeError("injected failure")):
                with self.assertRaises(RuntimeError):
                    verify.main()
            result = json.loads(summary.read_text())
            self.assertEqual(result["status"], "FAIL")
            self.assertNotEqual(result["run_id"], "OLD")
            self.assertEqual(result["results"], [])
            self.assertIn("injected failure", result["error"])


if __name__ == "__main__":
    unittest.main()
