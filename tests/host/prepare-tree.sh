#!/bin/bash
# Rebuild the patched driver sources the host tests compile:
#   tests/host/prepare-tree.sh /path/to/ios-lavender [out-dir]
#
# Applies the base series (ios-lavender xnu/patches, pinned commit
# 83356567124670f50a320b8ecc7c04f7ccf8f8e0) followed by this repository's
# patches/ that the base does not already carry (0192, 0200, 0201, ...),
# with git apply, only for bsd/dev/sweet_* and bsd/dev/santoni_sdhci.c.
# CR line endings from a Windows clone (core.autocrlf=true) are stripped
# first; git apply would otherwise reject the series.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
base="$(cd "${1:?path to an ios-lavender checkout}" && pwd)"
out="${2:-$here/out/tree}"

if [ -e "$out/bsd" ]; then
	echo "$out already holds a tree; pass a new directory" >&2
	exit 1
fi
mkdir -p "$out"
series="$(mktemp)"
trap 'rm -f "$series"' EXIT
for p in "$base"/xnu/patches/*.patch; do
	tr -d '\r' < "$p" >> "$series"
done
for p in "$repo"/patches/*.patch; do
	name="$(basename "$p")"
	if [ -e "$base/xnu/patches/$name" ]; then
		continue                # already in the base series (e.g. 0191)
	fi
	tr -d '\r' < "$p" >> "$series"
done
if [ ! -d "$out/.git" ]; then
	git -C "$out" init -q
	git -C "$out" config core.autocrlf false
fi
git -C "$out" apply --include='bsd/dev/sweet_*' --include='bsd/dev/santoni_sdhci.c' \
	--whitespace=nowarn "$series"
echo "patched sources in $out/bsd/dev"
