#!/usr/bin/env python3
"""Preserve active work while fast-forwarding idle persistent branches."""
import json
import os
from pathlib import Path
import subprocess

BRANCHES = ("codex/master", "claude/master", "agent/experimental")


def api(path, method="GET", payload=None):
    command = ["gh", "api", "--method", method, path]
    if payload is not None:
        command += ["--input", "-"]
    result = subprocess.run(command, input=json.dumps(payload) if payload else None,
                            text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    return json.loads(result.stdout)


def synchronize(repo, request=api):
    root = f"repos/{repo}"
    master = request(f"{root}/git/ref/heads/master")["object"]["sha"]
    rows = []
    for branch in BRANCHES:
        try:
            tip = request(f"{root}/git/ref/heads/{branch}")["object"]["sha"]
        except RuntimeError as error:
            if "HTTP 404" not in str(error):
                raise
            rows.append((branch, "Missing; owner must decide whether to restore it."))
            continue
        comparison = request(f"{root}/compare/{tip}...{master}")
        status = comparison["status"]
        if status == "ahead":  # master is ahead of this branch
            try:
                request(f"{root}/git/refs/heads/{branch}", "PATCH",
                        {"sha": master, "force": False})
                message = f"Fast-forwarded to {master[:12]}."
            except RuntimeError as error:
                if "HTTP 422" not in str(error) and "HTTP 409" not in str(error):
                    raise
                # A concurrent push can make the requested update non-fast-forward.
                # Report it; never use force or retry against a different tip.
                message = "Update refused; inspect current refs and merge origin/master if needed."
        elif status == "identical":
            message = "Already synchronized."
        elif status == "behind":
            message = "Contains this master snapshot; active work preserved."
        elif status == "diverged":
            message = "Diverged; branch owner must merge origin/master before the next slice."
        else:
            raise RuntimeError(f"Unexpected comparison status: {status}")
        rows.append((branch, message))
    return master, rows


def main():
    master, rows = synchronize(os.environ["GH_REPO"])
    summary = f"## Persistent agent branches\n\nMaster snapshot: `{master}`\n\n"
    summary += "| Branch | Result / next action |\n| --- | --- |\n"
    for branch, message in rows:
        summary += f"| `{branch}` | {message} |\n"
    print(summary)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as output:
            output.write(summary)


if __name__ == "__main__":
    main()
