# Official V8 jitless intake for OpenOS

This checklist tracks the Chromium-route P5 task: integrating an official V8 `d8`/shell minimum build that can run in jitless mode.

## Scope

- Use the GitHub V8 source (`https://github.com/v8/v8.git`) as the primary source, with `https://chromium.googlesource.com/v8/v8.git` as a fallback remote only when GitHub is unavailable.
- Build host Linux `d8` first as a dependency and behavior gate for the OpenOS Chromium route.
- Run a jitless smoke command:

```bash
.openos-deps/v8/out/openos-host-jitless/d8 --jitless -e 'print(1 + 2)'
```

Expected output:

```text
3
```

## Commands

```bash
./build.sh v8-official-check
./build.sh v8-official-fetch
./build.sh v8-official-build
./build.sh v8-official-smoke
```

Direct script usage:

```bash
scripts/v8-official.sh --check
scripts/v8-official.sh --fetch
scripts/v8-official.sh --sync-deps
scripts/v8-official.sh --build
scripts/v8-official.sh --smoke
```

## Pin file

A successful fetch writes:

```text
ports/chromium-openos/v8.official.pin
```

The pin records the official component name, repository URL, commit hash, source acquisition kind (`git-shallow-partial-checkout` or `github-tarball`), source path, GN output path, and jitless smoke command.

## Current boundary

This task does not claim OpenOS can already run full Chromium JavaScript workloads. It only establishes the official V8 source/build/smoke gate needed before replacing any local placeholder shell behavior.

## Network fallback

If a shallow Git checkout from GitHub is interrupted by the local network, `scripts/v8-official.sh --fetch` can fall back to the official GitHub codeload tarball for the resolved V8 commit and records `source_kind=github-tarball` in `ports/chromium-openos/v8.official.pin`.

Dependency syncing is GitHub-first for dependencies that have matching GitHub upstreams, and stores reusable archives plus a `.source` sidecar under `.openos-deps/v8-dep-cache`. A few Chromium split repositories used by V8 (`build`, `buildtools`, `tools/clang`, `tools/protoc_wrapper`) do not have authoritative GitHub mirrors for the pinned commits, so the script uses Chromium Gitiles archive URLs for those official pins. In this environment GitHub codeload is reachable, but `chromium.googlesource.com` currently times out, so `v8-official-sync-deps` can only complete once that network path is available or those archives are pre-seeded into the cache.

When a reusable Chromium/V8 checkout already exists locally, pass it as a seed directory before network dependency downloads:

```bash
OPENOS_V8_DEP_SEED_DIR=/path/to/chromium/src ./build.sh v8-official-sync-deps
```

The seed directory is expected to contain the same relative dependency paths (`build`, `buildtools`, `tools/clang`, and `tools/protoc_wrapper`). Seeded dependencies are copied into `.openos-deps/v8` and marked with `.openos-dep-*.done` files so interrupted downloads are not mistaken for complete syncs.
