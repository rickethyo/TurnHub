"""Maintenance must never overwrite divergent or concurrently updated work."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "sync", Path(__file__).with_name("sync-agent-branches.py"))
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


class MaintenanceTests(unittest.TestCase):
    def run_sync(self, statuses, update_error=None, missing=False):
        writes = []

        def request(path, method="GET", payload=None):
            if method == "PATCH":
                writes.append((path, payload))
                if update_error:
                    raise RuntimeError(update_error)
                return {}
            if path.endswith("heads/master"):
                return {"object": {"sha": "master-sha"}}
            if "/compare/" in path:
                branch = path.split("/compare/")[1].split("...")[0]
                return {"status": statuses[branch]}
            branch = path.split("heads/")[1]
            if missing:
                raise RuntimeError("HTTP 404: Not Found")
            return {"object": {"sha": branch}}

        result = sync.synchronize("owner/repo", request)
        return result, writes

    def test_only_ancestor_is_updated_without_force(self):
        (_, rows), writes = self.run_sync(dict(zip(sync.BRANCHES, ("ahead", "diverged"))))
        self.assertEqual(writes, [("repos/owner/repo/git/refs/heads/codex/master",
                                   {"sha": "master-sha", "force": False})])
        self.assertIn("Diverged", rows[1][1])
        (_, rows), writes = self.run_sync(dict(zip(sync.BRANCHES, ("behind", "behind"))))
        self.assertEqual(writes, [])
        self.assertTrue(all("preserved" in message for _, message in rows))

    def test_lvgl_and_legacy_stay_excluded_even_when_they_could_fast_forward(self):
        # Treat every candidate as an ancestor: exclusion must not depend on
        # today's divergence or whether a branch currently has an open PR.
        excluded = ("codex/esp32-s3-lvgl", "legacy/gen1", "agent/experimental")
        statuses = dict.fromkeys((*sync.BRANCHES, *excluded), "ahead")
        (_, rows), writes = self.run_sync(statuses)
        allowed = {"codex/master", "claude/master"}
        self.assertEqual({branch for branch, _ in rows}, allowed)
        self.assertEqual({path for path, _ in writes},
                         {f"repos/owner/repo/git/refs/heads/{branch}" for branch in allowed})
        self.assertTrue(all(name not in path for name in excluded for path, _ in writes))

    def test_identical_and_missing_are_left_alone(self):
        statuses = dict.fromkeys(sync.BRANCHES, "identical")
        self.assertEqual(self.run_sync(statuses)[1], [])
        (_, rows), writes = self.run_sync(statuses, missing=True)
        self.assertEqual(writes, [])
        self.assertTrue(all("Missing" in message for _, message in rows))

    def test_concurrent_update_is_not_retried_or_forced(self):
        (_, rows), writes = self.run_sync(dict.fromkeys(sync.BRANCHES, "ahead"),
                                          update_error="HTTP 422: not a fast forward")
        self.assertEqual(len(writes), len(sync.BRANCHES))
        self.assertTrue(all(not payload["force"] for _, payload in writes))
        self.assertTrue(all("refused" in message for _, message in rows))

    def test_api_failure_fails_instead_of_claiming_success(self):
        with self.assertRaisesRegex(RuntimeError, "HTTP 403"):
            self.run_sync(dict.fromkeys(sync.BRANCHES, "ahead"),
                          update_error="HTTP 403: Forbidden")


if __name__ == "__main__":
    unittest.main()
