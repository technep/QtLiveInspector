# Contributing to QtLiveInspector

Thanks for helping out. This document is the short version of how the project
is built, tested and released; the [README](README.md) has the long one.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Everything in the ctest suite must be green before a change lands:

- `JsonUtilTest`, `InputVisualizerTest` — C++ unit tests
- `ToolSmokeTest` — the Python tools against a stub probe
- `ProbeIntegration_*` — the real probe injected into the sample apps
  (quickdemo / quickviewdemo / widgetdemo), offscreen

Useful configurations while developing:

```bash
# warnings as errors (what CI enforces)
cmake -B build-werror -DQLI_WARNINGS_AS_ERRORS=ON

# sanitizers (what the CI sanitizer job runs)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined"
```

## Reproduce the CI environment locally

CI runs on `ubuntu-latest` — Ubuntu 24.04 with the distro Qt (6.4). When
your machine has a newer Qt (e.g. the online installer) and something fails
on CI that you cannot reproduce, `Dockerfile.ci` mirrors the CI toolchain:

```bash
docker build -f Dockerfile.ci -t qli-ci .
docker run --rm -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" \
    -e HOME=/tmp qli-ci bash -c \
    "cmake -G Ninja -B /tmp/build && cmake --build /tmp/build -j \
     && ctest --test-dir /tmp/build --output-on-failure"
```

The package list is a mirror of the install step in
`.github/workflows/ci.yml` — keep the two in sync when dependencies change.

## Cross-build the probe for ARM targets

The probe and inputviz libraries are injected into the target
application with `LD_PRELOAD`, so they must match the device's
architecture. `Dockerfile.probe` cross-compiles both for `arm64`
(aarch64) and `armhf` (ARMv7 hard-float) from an x86_64 host, using
Ubuntu's multiarch Qt packages:

```bash
docker build -f Dockerfile.probe -t qli-probe-cross .
mkdir -p dist && docker run --rm -v "$PWD":/src:ro -v "$PWD/dist":/out \
    -e QLI_BUILD_ROOT=/tmp/builds -w /src \
    qli-probe-cross docker/cross-build.sh /out
# dist/arm64/libqtinspectorprobe.so, libqtinspectorinputviz.so
# dist/armhf/libqtinspectorprobe.so, libqtinspectorinputviz.so
```

`docker/cross-build.sh` also runs outside Docker on any Debian/Ubuntu
host with the cross gccs and `qt6-base-dev:<arch>` /
`qt6-declarative-dev:<arch>` installed. The image is based on Ubuntu
22.04 so the libraries need only glibc 2.35 (jammy's Qt 6.2.4 is the
project's minimum); inject them only into target apps built against a
compatible Qt 6.

## Style

- C++: C++17, Qt-style API usage, 4-space indent, 80 columns. A
  `.clang-format` ships with the repo — run it on new code, but don't reformat
  untouched files.
- Python: 4-space indent; `ruff check tools/ tests/` must pass (config in
  `pyproject.toml`). Formatting is intentionally *not* enforced.
- Match the conventions of the file you are editing over any global rule.

## Versioning and releases

- One release unit: probe, inputviz, inspector and tools share the version in
  the top-level `project(... VERSION x.y.z)`.
- The Python tools carry the same version in `tools/qli_version.py`;
  **configure fails when the two drift** — bump both together.
- The wire protocol has its own `major.minor`, negotiated in the `hello`
  handshake; bump the major only for incompatible changes.
- Changelog entries follow [Keep a Changelog](https://keepachangelog.com):
  user-visible changes under `## [Unreleased]` with `Added` / `Changed` /
  `Fixed` / `Security` sections.
- To cut a release: rename `## [Unreleased]` to `## [x.y.z] — <date>` (leaving
  a fresh empty `## [Unreleased]` above), bump both version locations, and tag:
  `git tag -a vX.Y.Z -m "Release X.Y.Z"`.

## What makes a good change

- **Verified end-to-end.** A probe behavior change should come with an
  integration-test addition in `tests/integration_probe.py` (the PROFILES dict
  covers the widgets/quickview differences), not just a manual run.
- **Respects the security posture** — see [SECURITY.md](SECURITY.md). New
  mutating protocol commands must be gated by the token *and* rejected in
  read-only mode, and appear in the audit log.
- **Documented**: README table rows for new env vars/commands, CHANGELOG entry
  for anything a user would notice.
