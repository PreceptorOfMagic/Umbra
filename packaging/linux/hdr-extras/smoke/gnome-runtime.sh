#!/bin/bash
# Start Mutter headless (software rendering) with its own session bus and PipeWire, and run
# gnome-virtual-hdr-probe.py against it. Prints the probe's JSON; Mutter's log goes to $1.log.
#   gnome-runtime.sh <log-prefix>
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
log=${1:?usage: gnome-runtime.sh <log-prefix>}

export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
export LIBGL_ALWAYS_SOFTWARE=1 GSK_RENDERER=cairo NO_AT_BRIDGE=1

dbus-run-session -- bash -c '
  pipewire >"$1.pipewire.log" 2>&1 &
  sleep 1
  mutter --headless --wayland --no-x11 --wayland-display=umbra-smoke >"$1.log" 2>&1 &
  mutter_pid=$!
  for _ in $(seq 1 60); do
    gdbus introspect --session --dest org.gnome.Mutter.ScreenCast --object-path /org/gnome/Mutter/ScreenCast >/dev/null 2>&1 && break
    kill -0 $mutter_pid 2>/dev/null || { echo "{\"errors\": [\"mutter exited\"]}"; exit 1; }
    sleep 0.5
  done
  python3 "$2/gnome-virtual-hdr-probe.py"
  status=$?
  kill $mutter_pid 2>/dev/null
  exit $status
' sh "$log" "$here"
