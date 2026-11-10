#!/bin/bash
# Uploads every pinned download (pins.txt, scripts/cpan-pins.sha256 and
# source-pins.txt) as assets of this repository's "pinned-downloads" release,
# where build.sh and scripts/sources.sh look when an upstream URL fails.
# Termux deletes old .deb files soon after a version bump, so run this right
# after the first push and whenever a pin changes, while the files can still be
# fetched. The release is a prerelease, so it never shows as the latest version.
#
#   ./build.sh fetch && scripts/sources.sh && scripts/mirror-upload.sh [--dry-run]
#
# Needs the GitHub CLI (gh), logged in with write access to the repository.
# Files already in the release are skipped, so the release only ever grows and
# an old checkout can still find what it pinned.
set -euo pipefail
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}
TAG=pinned-downloads
REPO=${GITHUB_REPOSITORY:-$(gh repo view --json nameWithOwner -q .nameWithOwner)}
DRY=${1:-}

files=()
while read -r sha url name; do
  if [ -n "$sha" ]; then files+=("$W/dl/pinned/$name"); fi
done < <(grep -v '^#' $B/pins.txt)
while read -r sha url; do
  if [ -n "$sha" ]; then files+=("$W/dl/pinned/cpan/$(basename "$url")"); fi
done < <(grep -v '^#' $B/scripts/cpan-pins.sha256)
while read -r sha url name; do
  if [ -n "$sha" ] && [ "$sha" != termux-packages ]; then files+=("$W/dl/sources/$name"); fi
done < <(grep -v '^#' $B/source-pins.txt)

for f in "${files[@]}"; do [ -f "$f" ] || { echo "missing $f: run ./build.sh fetch and scripts/sources.sh first" >&2; exit 1; }; done

if ! gh release view $TAG --repo "$REPO" >/dev/null 2>&1; then
  notes="Copies of the files the build downloads, for when an upstream URL stops working. Every file is checked against the SHA-256 in pins.txt, scripts/cpan-pins.sha256 or source-pins.txt before use."
  if [ "$DRY" = --dry-run ]; then echo "would create release $TAG"
  else gh release create $TAG --repo "$REPO" --title "Pinned downloads" --notes "$notes" --prerelease --latest=false; fi
fi
have=$(gh release view $TAG --repo "$REPO" --json assets -q '.assets[].name' 2>/dev/null || true)
todo=()
for f in "${files[@]}"; do
  grep -qxF "$(basename "$f")" <<<"$have" || todo+=("$f")
done
echo "${#files[@]} pinned files, ${#todo[@]} not in the release yet"
[ ${#todo[@]} -gt 0 ] || exit 0
if [ "$DRY" = --dry-run ]; then printf 'would upload %s\n' "${todo[@]##*/}"; exit 0; fi
gh release upload $TAG --repo "$REPO" "${todo[@]}"
