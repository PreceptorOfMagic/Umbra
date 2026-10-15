#!/bin/sh
# KDE Plasma lets an app use its screen-cast protocol, which Umbra's per-stream virtual displays, Co-op Pane and
# HDR need on KDE, only when the app's desktop file asks for it (X-KDE-Wayland-Interfaces). Flatpak 1.18.4 and
# later export desktop files through an allowlist of keys that drops this one (CVE-2026-97027), so this puts a copy
# of Umbra's exported desktop file that keeps the key in the user's applications folder. Other desktops ignore the
# key. Where Flatpak still exports the key, no copy is needed and an old one is removed. "remove" deletes the copy.
# Run inside the installed Flatpak; Input Setup, the app-menu launcher and the removal helper call it.
set -eu
app_id=${FLATPAK_ID:?Run this helper inside the installed Flatpak}
case "$app_id" in *[!A-Za-z0-9._-]*|'') echo 'Invalid Flatpak application ID' >&2; exit 2;; esac
action=${1:-install}
case "$action" in install|remove) ;; *) echo 'Usage: umbra-kde-entry.sh [install|remove]' >&2; exit 2;; esac
# The exported desktop files are on the host, outside the sandbox, so the copy is written there.
flatpak-spawn --host sh -c '
  set -eu
  id=$1 key=$2 action=$3
  data=${XDG_DATA_HOME:-$HOME/.local/share}
  dest=$data/applications/$id.desktop
  # KWin finds the desktop file through KDE'"'"'s service cache, which otherwise catches up a few seconds later
  # (measured: a launch right after writing the copy still fell back to the portal). Refresh it now.
  refresh() { if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 >/dev/null 2>&1 || true; fi; }
  if [ "$action" = remove ]; then
    if grep -qx "$key" "$dest" 2>/dev/null; then rm -f "$dest"; refresh; fi
    exit 0
  fi
  src=
  for dir in "$data/flatpak/exports/share/applications" /var/lib/flatpak/exports/share/applications; do
    if [ -r "$dir/$id.desktop" ]; then src=$dir/$id.desktop; break; fi
  done
  if [ -z "$src" ]; then
    echo "Umbra'"'"'s exported desktop file was not found, so KDE may not grant its virtual displays." >&2
    exit 1
  fi
  if grep -qx "$key" "$src"; then
    if grep -qx "$key" "$dest" 2>/dev/null; then rm -f "$dest"; refresh; fi
    exit 0
  fi
  mkdir -p "$data/applications"
  tmp=$(mktemp "$data/applications/.$id.XXXXXX")
  trap '"'"'rm -f "$tmp"'"'"' EXIT
  sed "/^X-KDE-Wayland-Interfaces=/d; /^\[Desktop Entry\]\$/a $key" "$src" > "$tmp"
  grep -qx "$key" "$tmp"
  chmod 0644 "$tmp"
  # KWin reloads the refreshed cache shortly afterwards: Umbra started at once still fell back to the portal,
  # 2 s later it got the virtual displays (measured twice each, Fedora 44 / KWin 6.7.5). Only when the copy changes.
  if ! cmp -s "$tmp" "$dest"; then mv -f "$tmp" "$dest"; refresh; sleep 2; fi
' sh "$app_id" X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1 "$action"
