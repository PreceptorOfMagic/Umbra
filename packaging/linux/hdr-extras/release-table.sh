#!/bin/bash
# Markdown table of the HDR extras in a directory, one row per extra, for the release notes:
#
#   release-table.sh <directory with Umbra-HDR-*.tar.gz>
#
# What each fits comes from its own README.txt. How far each has been tested on hardware comes from
# hardware-tested, as in the TESTED section build.sh writes.
set -euo pipefail

dir=${1:?usage: release-table.sh <directory>}
here=$(cd "$(dirname "$0")" && pwd)

hardware() { # <target> <component> <package version>
  local ran how own
  read -r ran how own < <(awk -v t="$1" -v c="$2" '!/^#/ && $1 == t && $2 == c { print $3, $4, $5 }' "$here/hardware-tested") || true
  if [ -z "$ran" ]; then
    case "$1" in
      arch|cachyos) echo "None: Arch Linux and CachyOS cannot start on the test PC without changing its Secure Boot setup" ;;
      *) echo "None" ;;
    esac
    return
  fi
  local what="Umbra 0.5.2 on an AMD Radeon RX Vega: HDR co-op pane; HDR desktop stream and NVIDIA untested"
  if [ "$ran" != "$3" ]; then echo "Version \`$ran\` ran with $what. This version has not run on hardware"; return; fi
  case "$how" in
    local) printf 'Yes, as a build of this version made on the test PC, with %s' "$what" ;;
    *) printf 'Yes, with %s' "$what" ;;
  esac
  case "$1/$2" in
    ubuntu26.04/kwin) echo ". The patch also ran with Umbra 0.5.1 on an NVIDIA RTX 2060 SUPER (HDR desktop and co-op)" ;;
    ubuntu26.04/mutter) echo ". gnome-shell memory flat over seven HDR stream starts and stops. Revision 1 also ran with Umbra 0.5.1 on an NVIDIA RTX 2060 SUPER (HDR desktop and co-op)" ;;
    *) echo ;;
  esac
}

echo "| Download | For | Package version | Tested on hardware | Automated test |"
echo "|---|---|---|---|---|"
while read -r target _ _ desktops; do
  for desktop in $desktops; do
    case $desktop in kde) d=KDE component=kwin ;; gnome) d=GNOME component=mutter ;; *) continue ;; esac
    f="$dir/Umbra-HDR-$d-$target.tar.gz"
    [ -f "$f" ] || { echo "error: $f is missing" >&2; exit 1; }
    readme=$(tar -xzOf "$f" --wildcards '*/README.txt')
    title=$(head -n1 <<<"$readme")                         # ... - <distribution> (<Compositor> <version>)
    distro=$(sed -n 's/^.* - \(.*\) (.*)$/\1/p' <<<"$title")
    compositor=$(sed -n 's/^.*(\(.*\))$/\1/p' <<<"$title")
    version=$(sed -n 's/^PACKAGES (version \(.*\))$/\1/p' <<<"$readme")
    echo "| \`$(basename "$f")\` | $distro, $compositor | \`$version\` | $(hardware "$target" "$component" "$version") | Passed |"
  done
done < <(grep -v '^#' "$here/targets" | awk 'NF >= 4')
