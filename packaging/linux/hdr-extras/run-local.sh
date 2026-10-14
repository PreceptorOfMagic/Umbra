#!/bin/bash
# Build one HDR extra the way CI does, in a container of the target distribution:
#
#   run-local.sh <target> <kde|gnome> [output-dir]
#
# Needs podman or docker (CONTAINER_ENGINE picks one). The checkout is mounted read-only; the
# tarballs land in output-dir (default ./hdr-extras-out), owned by the calling user.
set -euo pipefail

target=${1:?usage: run-local.sh <target> <kde|gnome> [output-dir]}
desktop=${2:?usage: run-local.sh <target> <kde|gnome> [output-dir]}
out=${3:-hdr-extras-out}

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
engine=${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$engine" ] || { echo "error: needs podman or docker" >&2; exit 1; }

image=$(awk -v t="$target" '$1 == t { print $2 }' "$here/targets")
[ -n "$image" ] || { echo "error: unknown target $target (see $here/targets)" >&2; exit 1; }

mkdir -p "$out"
out=$(cd "$out" && pwd)

"$engine" pull "$image"
"$engine" run --rm \
  -v "$repo:/src:ro" -v "$out:/out" \
  -e UMBRA_REF="${UMBRA_REF:-}" -e HOST_IDS="$(id -u):$(id -g)" \
  "$image" bash -c '
    set -e
    status=0
    bash /src/packaging/linux/hdr-extras/build.sh "$1" "$2" /out || status=$?
    chown -R "$HOST_IDS" /out
    exit $status
  ' sh "$desktop" "$target"
