## Upstream snapshot testing (build + compare)

This doc is intentionally **only** about upstream testing workflows. It’s the place to put:

- how to build a “mostly upstream” snapshot for A/B testing
- how to diff upstream vs this fork reproducibly

All “what changed in the fork” narrative lives in `docs/WORKLOG_AND_POLISH_NOTES.md`.

### Option A: compare using the upstream-test folder

This repo keeps a local upstream reference folder for reproducible diffs.

Set two environment variables to point at your local checkouts (don’t paste your personal home directory paths into docs/issues):

```bash
export YORADIO_UPSTREAM_DIR="/path/to/yoRadio-fusion-pros3-upstream-test/yoRadio"
export YORADIO_FORK_DIR="/path/to/yoRadio-fusion-pros3"
```

Diff only `src/` (fast, high-signal):

```bash
git diff --no-index --stat \
  "${YORADIO_UPSTREAM_DIR}/src" \
  "${YORADIO_FORK_DIR}/src"

git diff --no-index --name-status \
  "${YORADIO_UPSTREAM_DIR}/src" \
  "${YORADIO_FORK_DIR}/src"
```

Notes:
- That upstream-test snapshot may not include fork-only root docs and `tools/` scripts — it’s primarily for `src/` comparisons.

### Option B: build an “upstream snapshot” branch in this repo

The remotes include branches intended for upstream testing (so you can flash an upstream-ish build without switching workspaces).

Examples:

```bash
# Fetch latest
git fetch origin

# Inspect available testing branches
git branch -r | rg \"upstream\"

# Check out an upstream snapshot branch locally
git checkout -B upstream-snapshot origin/upstream-snapshot

# Build it
pio run -e yoradio-um_pros3-ili9341
```

If you need “upstream + myoptions.h” for hardware bring-up while minimizing code deltas, use `origin/upstream-with-myoptions` similarly.

