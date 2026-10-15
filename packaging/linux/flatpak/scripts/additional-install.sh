#!/bin/sh
# Optional host integration. Run inside the installed Flatpak.
set -eu
app_id=${FLATPAK_ID:?Run this helper inside the installed Flatpak}
case "$app_id" in *[!A-Za-z0-9._-]*|'') echo 'Invalid Flatpak application ID' >&2; exit 2;; esac
rules=/app/share/sunshine/udev/rules.d/60-sunshine.rules
unit=/app/share/sunshine/systemd/user/sunshine.service
[ -r "$rules" ] && [ -r "$unit" ] || { echo 'The Flatpak is missing its integration files.' >&2; exit 1; }

# KDE: the desktop-file key KDE needs to grant Umbra's virtual displays (umbra-kde-entry.sh); no admin prompt.
/app/bin/umbra-kde-entry.sh || echo 'KDE virtual displays may stay unavailable; run Input Setup again to retry.' >&2

echo 'Your desktop will ask for administrator permission to enable streamed input.'
if ! flatpak-spawn --host pkexec sh -c '
  set -eu
  dest=$1
  tmp=$(mktemp /etc/udev/rules.d/.umbra-input.XXXXXX)
  trap '\''rm -f "$tmp"'\'' EXIT
  trap '\''exit 130'\'' INT
  trap '\''exit 143'\'' HUP TERM
  cat > "$tmp"
  [ -s "$tmp" ]
  chmod 0644 "$tmp"
  mv -f "$tmp" "$dest"
  # A static /dev node may exist before its module is loaded. Such a node has
  # no sysfs device to trigger, so checking /dev alone can leave access denied.
  modprobe uinput
  modprobe uhid
  udevadm control --reload-rules
  udevadm trigger --action=add --subsystem-match=misc --sysname-match=uinput
  udevadm trigger --action=add --subsystem-match=misc --sysname-match=uhid
  udevadm settle
' sh "/etc/udev/rules.d/60-$app_id.rules" < "$rules"; then
  echo 'Input setup did not finish. Check that your desktop has a polkit authentication agent, then run setup again.' >&2
  exit 1
fi

# Give this app its own unit; never replace a native Sunshine installation.
mkdir -p "$HOME/.config/systemd/user"
cp "$unit" "$HOME/.config/systemd/user/$app_id.service"
if flatpak-spawn --host systemctl --user daemon-reload; then
  echo "Optional autostart: systemctl --user enable --now $app_id.service"
else
  echo 'This desktop could not load the optional systemd user service. Launch the app normally.' >&2
fi
if [ ! -w /dev/uinput ] || [ ! -w /dev/uhid ]; then
  echo 'The rules were installed, but this session still lacks input access. Log out and back in, then restart the app.' >&2
  exit 1
fi
echo 'Input rules are installed and device access checks passed. Restart the app to detect the devices.'
