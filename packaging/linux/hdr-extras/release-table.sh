#!/bin/bash
# Markdown table of the HDR extras in a directory, one row per extra, for the release notes:
#
#   release-table.sh <directory with Umbra-HDR-*.tar.gz>
#
# What each fits comes from its own README.txt. How far each has been tested comes from the list
# below, by distribution and patch series; it matches the TESTED section build.sh writes.
set -euo pipefail

dir=${1:?usage: release-table.sh <directory>}
here=$(cd "$(dirname "$0")" && pwd)

hardware() { # <target> <component> <series>
  case "$1/$2-$3" in
    ubuntu26.04/kwin-6.6) echo "Yes, with Umbra 0.5.1 (NVIDIA RTX 2060 SUPER, AMD Radeon RX Vega). Not yet with Umbra 0.5.2" ;;
    ubuntu26.04/mutter-50) echo "Patch revision 1, with Umbra 0.5.1 (NVIDIA RTX 2060 SUPER, AMD Radeon RX Vega). This revision 2 (two memory-leak fixes): automated test only" ;;
    */mutter-50) echo "None. The same patch ran on hardware on Ubuntu 26.04 (revision 1)" ;;
    *) echo "None. This patch has not run on hardware anywhere" ;;
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
    upstream=${compositor##* }
    case $component in kwin) series=$(cut -d. -f1-2 <<<"$upstream") ;; mutter) series=${upstream%%.*} ;; esac
    version=$(sed -n 's/^PACKAGES (version \(.*\))$/\1/p' <<<"$readme")
    echo "| \`$(basename "$f")\` | $distro, $compositor | \`$version\` | $(hardware "$target" "$component" "$series") | Passed |"
  done
done < <(grep -v '^#' "$here/targets" | awk 'NF >= 4')
