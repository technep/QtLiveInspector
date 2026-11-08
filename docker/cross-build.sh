#!/usr/bin/env bash
# Cross-build the probe and inputviz libraries for ARM targets.
#
# Usage (from the repository root):
#   docker/cross-build.sh [OUTDIR]     # default OUTDIR: dist
#
# Produces OUTDIR/arm64/  with libqtinspectorprobe.so / libqtinspectorinputviz.so
#          OUTDIR/armhf/  same, 32-bit ARMv7 hard-float
#
# The libraries link the distro Qt for the target architecture, so on the
# device they must be injected (LD_PRELOAD) into a target app built against
# a compatible Qt 6.
#
# QLI_BUILD_ROOT selects where the CMake build trees go; the read-only
# Docker invocation in Dockerfile.probe points it at /tmp/builds.
set -euo pipefail
cd "$(dirname "$0")/.."

OUTDIR="${1:-dist}"
BUILD_ROOT="${QLI_BUILD_ROOT:-$PWD}"

# toolchain file -> short output name
BUILDS="aarch64-linux-gnu:arm64 armhf-linux-gnueabihf:armhf"

for entry in $BUILDS; do
    tc="${entry%%:*}"
    short="${entry##*:}"
    build_dir="$BUILD_ROOT/build-cross-$short"
    cmake -G Ninja -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
        -DQLI_BUILD_SAMPLES=OFF \
        -DCMAKE_TOOLCHAIN_FILE="cmake/toolchain-$tc.cmake"
    cmake --build "$build_dir" --target qtinspectorprobe qtinspectorinputviz -j"$(nproc)"
    mkdir -p "$OUTDIR/$short"
    cp "$build_dir/probe/libqtinspectorprobe.so" \
       "$build_dir/inputviz/libqtinspectorinputviz.so" \
       "$OUTDIR/$short/"
    echo "built $short -> $OUTDIR/$short/"
done
