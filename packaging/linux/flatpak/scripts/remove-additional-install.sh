#!/bin/sh
# Remove only integration installed for this Flatpak identity.
set -eu
app_id=${FLATPAK_ID:?Run this helper inside the installed Flatpak}
case "$app_id" in *[!A-Za-z0-9._-]*|'') echo 'Invalid Flatpak application ID' >&2; exit 2;; esac
if ! flatpak-spawn --host pkexec sh -c '
  set -eu
  rm -f "$1"
  udevadm control --reload-rules
' sh "/etc/udev/rules.d/60-$app_id.rules"; then
  echo 'Input-rule removal did not finish; the user service has been retained.' >&2
  exit 1
fi
flatpak-spawn --host systemctl --user disable --now "$app_id.service" 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/$app_id.service"
rm -f "$HOME/.config/autostart/$app_id.desktop"
flatpak-spawn --host systemctl --user daemon-reload || true
echo 'Optional host integration removed. Existing device permissions reset on logout or reboot.'
