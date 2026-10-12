#!/bin/sh
# Per-user XDG desktop autostart, including desktops without systemd session targets.
set -eu
app_id=${FLATPAK_ID:?Run this helper inside the installed Flatpak}
case "$app_id" in *[!A-Za-z0-9._-]*|'') echo 'Invalid Flatpak application ID' >&2; exit 2;; esac
directory=$HOME/.config/autostart
entry=$directory/$app_id.desktop
case "${1:-}" in
  enable)
    mkdir -p "$directory"
    tmp=$(mktemp "$directory/.umbra-autostart.XXXXXX")
    trap 'rm -f "$tmp"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' HUP TERM
    cat > "$tmp" <<EOF
[Desktop Entry]
Type=Application
Name=Umbra
Comment=Start the Umbra streaming host at desktop login
Exec=flatpak run --command=sunshine $app_id
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    chmod 0644 "$tmp"
    mv -f "$tmp" "$entry"
    ;;
  disable) rm -f "$entry" ;;
  *) echo 'Usage: umbra-autostart.sh enable|disable' >&2; exit 2 ;;
esac
# Migrate this app's optional service to desktop startup without stopping an
# already running stream. Desktops without systemd can still use the entry.
flatpak-spawn --host systemctl --user disable "$app_id.service" >/dev/null 2>&1 || true
