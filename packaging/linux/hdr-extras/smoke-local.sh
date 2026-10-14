#!/bin/bash
# Smoke-test one built HDR extra the way CI does, in a fresh container of its distribution:
#
#   smoke-local.sh <target> <kde|gnome> [directory with the built tarballs]
#
# Needs podman or docker. KDE: with a KMS device on the host (for example `modprobe vkms`), the
# container gets /dev/dri and runs privileged so KWin's DRM backend can drive it.
set -euo pipefail

target=${1:?usage: smoke-local.sh <target> <kde|gnome> [dir]}
desktop=${2:?usage: smoke-local.sh <target> <kde|gnome> [dir]}
dir=${3:-hdr-extras-out}

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
engine=${CONTAINER_ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$engine" ] || { echo "error: needs podman or docker" >&2; exit 1; }
image=$(awk -v t="$target" '$1 == t { print $2 }' "$here/targets")
[ -n "$image" ] || { echo "error: unknown target $target" >&2; exit 1; }
dir=$(cd "$dir" && pwd)

# KWin's binaries carry cap_sys_nice; without it in the container they cannot be executed.
args=(--cap-add SYS_NICE)
if [ "$desktop" = kde ] && ls /dev/dri/card* >/dev/null 2>&1; then
  args+=(--privileged -v /dev/dri:/dev/dri)
fi

"$engine" pull -q "$image" >/dev/null
"$engine" run --rm "${args[@]}" -v "$repo:/src:ro" -v "$dir:/in:ro" -e UMBRA_SMOKE_REQUIRE_KMS="${UMBRA_SMOKE_REQUIRE_KMS:-}" "$image" \
  bash /src/packaging/linux/hdr-extras/smoke-test.sh "$desktop" "$target" /in
