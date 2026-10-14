#!/bin/bash
# Start KWin without a desktop session and run kde-virtual-hdr-probe against it.
#   kde-runtime.sh <virtual|drm> <log-prefix> <umbra-checkout>
# virtual: KWin's headless backend (software QPainter compositing). Its virtual outputs are its own
#   kind, not the DRM backend's the patch changes, so this shows only that KWin starts, loads the
#   screencast plugin and streams a virtual output (8-bit) as Umbra asks for it.
# drm: KWin's DRM backend on a KMS device (vkms in CI) with software OpenGL: the real path, where
#   the patch makes the virtual output HDR-capable and the stream offer 10-bit formats.
# Prints one JSON object (the probe's, plus "formats" and "ten_bit_formats").
set -uo pipefail
mode=${1:?usage: kde-runtime.sh <virtual|drm> <log-prefix> <umbra-checkout>}
log=${2:?}
repo=${3:?}
here=$(cd "$(dirname "$0")" && pwd)

tmp=$(mktemp -d)
for p in zkde-screencast-unstable-v1 kde-output-device-v2 kde-output-management-v2; do
  wayland-scanner client-header "$repo/third-party/kde-output-protocols/$p.xml" "$tmp/$p.h" &&
    wayland-scanner private-code "$repo/third-party/kde-output-protocols/$p.xml" "$tmp/$p.c" || exit 1
done
gcc -O1 -o "$tmp/probe" -I"$tmp" -I"$repo/src/platform/linux" "$here/kde-virtual-hdr-probe.c" \
  "$repo/src/platform/linux/kde-output.c" "$tmp"/*.c -lwayland-client >"$log.cc.log" 2>&1 ||
  { echo '{"error": "probe did not compile"}'; cat "$log.cc.log" >&2; exit 1; }

export XDG_RUNTIME_DIR="$tmp/run"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export LIBGL_ALWAYS_SOFTWARE=1 KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 QT_LOGGING_RULES='kwin_*.debug=true'
case $mode in
  virtual) kwin_args=(--virtual --width 1280 --height 720) ;;
  drm) export KWIN_COMPOSE=O2; kwin_args=(--drm) ;;
  *) echo '{"error": "mode must be virtual or drm"}'; exit 1 ;;
esac

dbus-run-session -- bash -c '
  probe=$1 log=$2 here=$3; shift 3
  pipewire >"$log.pipewire.log" 2>&1 &
  sleep 1
  kwin_wayland "$@" --no-lockscreen --no-global-shortcuts --socket umbra-smoke >"$log.log" 2>&1 &
  kwin=$!
  for _ in $(seq 1 60); do
    [ -S "$XDG_RUNTIME_DIR/umbra-smoke" ] && break
    kill -0 $kwin 2>/dev/null || { echo "{\"error\": \"kwin_wayland exited\"}"; exit 1; }
    sleep 0.5
  done
  sleep 2
  coproc PROBE { WAYLAND_DISPLAY=umbra-smoke "$probe"; }
  probe_pid=$PROBE_PID  # bash clears PROBE_PID once the probe exits
  read -r -t 30 line <&"${PROBE[0]}" || line="{\"error\": \"probe gave no answer\"}"
  sleep 2
  node=$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get(\"node_id\", \"\"))" "$line" 2>/dev/null)
  formats="[]"
  [ -n "$node" ] && formats=$(python3 "$here/pw_formats.py" "$node")
  [ -n "${PROBE[1]:-}" ] && eval "exec ${PROBE[1]}>&-"
  [ -n "$probe_pid" ] && wait "$probe_pid" 2>/dev/null
  kill $kwin 2>/dev/null
  python3 -c "
import json, sys
r = json.loads(sys.argv[1]); f = json.loads(sys.argv[2])
r[\"formats\"] = f
r[\"ten_bit_formats\"] = sorted(set(f) & {\"xRGB_210LE\", \"xBGR_210LE\", \"ARGB_210LE\", \"ABGR_210LE\"})
print(json.dumps(r))" "$line" "$formats"
' sh "$tmp/probe" "$log" "$here" "${kwin_args[@]}"
