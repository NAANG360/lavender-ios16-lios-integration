#!/bin/bash
# Build a lavender shim with the known-good boot arguments.
#
# Usage:
#   ./build-shim.sh /path/to/ios-lavender /path/to/kernel.development.lavender
#
# Requires clang, ld.lld, llvm-objcopy/readelf/nm and python3. The base tree
# must contain resources/lavender_dtb.bin and the shim/ directory.
set -euo pipefail

BASE="${1:?path to the ios-lavender checkout}"
KERNEL="${2:?path to kernel.development.lavender}"
BOARD="${BOARD:-lavender}"
ROOTDEV="${ROOTDEV:-emmcroot0}"

# The lock-screen boot command line. Adjust only one variable at a time.
EXTRA_CMDLINE="${EXTRA_CMDLINE:-sweetlog=1 emmcrw=1 rootrw=1 cs_enforcement_disable=1 unentitled_ios_sim_launch=1 launchd_unsecure_cache=1 wdt=-1 wdt_test=no_user_checkin sweetfb_noconsole=1 sweetfb_stayon=1 sweetfb_hz=30 cpus=4}"
KPRINTF_SPAM="${KPRINTF_SPAM-}"

[ -f "$KERNEL" ] || { echo "missing kernel: $KERNEL" >&2; exit 1; }
[ -f "$BASE/shim/build.sh" ] || { echo "missing $BASE/shim/build.sh" >&2; exit 1; }

cd "$BASE"
BOARD="$BOARD" \
ROOTDEV="$ROOTDEV" \
EXTRA_CMDLINE="$EXTRA_CMDLINE" \
KPRINTF_SPAM="$KPRINTF_SPAM" \
bash shim/build.sh "$KERNEL"

ls -l shim/out/shim-boot.img
sha256sum shim/out/shim-boot.img
