# Agent working environment

Firmware commands must use the project virtual environment:

```bash
tools/venv/bin/python3 tools/grinder.py build --hardware v1 --jobs 8
tools/venv/bin/python3 tools/grinder.py build --hardware v2 --jobs 8
```

The V1 and V2 PlatformIO caches are deliberately separate. Do not override them
with one shared cache. The desktop simulator in `sim/` uses its own build
scripts; see `sim/README.md`.

Before changing code, read `CLAUDE.md` and the relevant complete source files.
After changes, run the host regression tests
(`python3 -m unittest discover -s tools/tests -p '*_test.py'`, plus the
`node tools/tests/*_web_test.mjs` page tests), the appropriate simulator tests,
both firmware builds when shared code changed, `git diff --check`, and update
user-facing documentation.

## GitHub write verification

When posting or editing pull request descriptions, issue comments, review
comments, or release notes:

- Do not pass multiline text through nested shell quoting. An outer shell can
  reduce the body to `$` or remove substitutions.
- Prefer structured GitHub connector or API fields for the body text.
- Immediately read the published object back from GitHub and compare its visible
  body with the intended text. An API success response is not publication proof.
- Repair malformed text before continuing with the wider task.

## Completion protocol

Before ending a coding session or reporting that a task is finished:

1. Re-read the user's current request and the active task plan.
2. Reconcile every agreed item as completed, deliberately deferred with a
   reason, or blocked by a specific external dependency.
3. Check every relevant worktree and branch for uncommitted or unpushed work,
   and check any open PRs or CI runs that are part of the task.
4. Run the required formatting, tests, V1/V2 builds, documentation checks, and
   hardware or OTA validation appropriate to the changed scope.
5. Report the complete outcome and list anything that remains. Never treat an
   answered side question, an interruption, a successful compile, or one
   completed subtask as completion of the wider task.

If work is interrupted by a user question, answer it and then resume the active
plan unless the user explicitly replaces or cancels that plan.
