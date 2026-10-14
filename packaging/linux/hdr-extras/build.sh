#!/bin/bash
# Build one HDR extra: the target distribution's own kwin (kde) or mutter (gnome) package with
# Umbra's virtual-display HDR patch added, packed as Umbra-HDR-<KDE|GNOME>-<target>.tar.gz with a
# README.txt and SHA256SUMS, plus the matching source tarball.
#
#   build.sh <kde|gnome> <target> <output-dir>
#
# Runs as root inside a container of the target's image (see targets; run-local.sh starts one).
# Fails, rather than guessing, when the distribution's version has no patch or its binary and
# source packages disagree.
set -euo pipefail

# All of it in main(): bash then parses the whole file before running any of it.
main() {

desktop=${1:?usage: build.sh <kde|gnome> <target> <output-dir>}
target=${2:?usage: build.sh <kde|gnome> <target> <output-dir>}
out=${3:?usage: build.sh <kde|gnome> <target> <output-dir>}

here=$(cd "$(dirname "$0")" && pwd)
linux=$(dirname "$here")
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=${WORK_DIR:-/tmp/umbra-hdr-build}
rm -rf "$work"
mkdir -p "$work"

# Where the patch and its README live, for the README.txt links: a tag for a release, else the branch.
umbra_ref=${UMBRA_REF:-release/umbra-linux-0.5}
umbra_tree="https://github.com/PreceptorOfMagic/Umbra/tree/$umbra_ref/packaging/linux"

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Plain bash until fam_setup has installed the tools (openSUSE's image has no awk).
family=
while read -r name _image fam rest; do
  [ "$name" = "$target" ] && { family=$fam; desktops=$rest; break; }
done < <(grep -v '^#' "$here/targets")
[ -n "$family" ] || die "unknown target $target (see $here/targets)"
[[ " $desktops " == *" $desktop "* ]] || die "$target has no $desktop extra (see $here/targets)"

case $desktop in
  kde)
    component=kwin
    desktop_name="KDE Plasma"
    compositor=KWin
    upstream_org=KDE
    umbra_cache=kde_virtual_hdr
    patch_dir=$linux/kwin-virtual-hdr
    ;;
  gnome)
    component=mutter
    desktop_name=GNOME
    compositor=Mutter
    upstream_org=GNOME
    umbra_cache=gnome_virtual_hdr
    patch_dir=$linux/mutter-virtual-hdr
    ;;
  *) die "desktop must be kde or gnome" ;;
esac

# Patch series: KWin by major.minor (6.7), Mutter by major (51).
series_of() {
  case $component in
    kwin) echo "$1" | cut -d. -f1-2 ;;
    mutter) echo "$1" | cut -d. -f1 ;;
  esac
}

# What has been tested, per target and patch series, in three parts: hardware (only Ubuntu 26.04 has
# run on hardware), the automated test every build passes before release, and what no test covers.
tested_note() {
  echo "On hardware:"
  case "$target/$component-$series" in
    ubuntu26.04/kwin-6.6)
      echo "  Ubuntu 26.04.1, KDE Plasma 6.6 on Wayland, with Umbra 0.5.1 on an NVIDIA RTX 2060 SUPER"
      echo "  (driver 595.99.02) and an AMD Radeon RX Vega: HDR streams, as measured in the KWin patch's"
      echo "  README. Umbra 0.5.2 has not run on hardware with these packages; its changed KDE output"
      echo "  control is covered only by the automated test below."
      ;;
    ubuntu26.04/mutter-50)
      echo "  Ubuntu 26.04.1, GNOME 50.1 on Wayland, with Umbra 0.5.1 on an NVIDIA RTX 2060 SUPER"
      echo "  (driver 595.99.02) and an AMD Radeon RX Vega, as measured in the Mutter patch's README:"
      echo "  revision 1 of the patch. These packages carry revision 2, which adds only the two"
      echo "  memory-leak fixes Mutter made to the code the patch copies; revision 2 has run only in"
      echo "  the automated test below."
      ;;
    *)
      echo "  None. These packages have not run on a real PC. $(series_status)"
      ;;
  esac
  echo "Automated, for every build before release:"
  echo "  installed over the distribution's own packages in a clean $distro_name container,"
  echo "  checked with the commands above and rolled back again. In between, $compositor runs without"
  if [ "$component" = mutter ]; then
    echo "  a GPU (software rendering) and must offer HDR and 10-bit capture on the virtual monitor"
    echo "  Umbra asks for, as stock Mutter must not."
  else
    echo "  a GPU, on a virtual KMS device with software rendering, and Umbra's own output control must"
    echo "  find and resize the virtual output Umbra asks for, make it HDR-capable, switch it to HDR"
    echo "  and get 10-bit capture, as stock KWin must not."
  fi
  echo "Not covered by the automated test: a real GPU or display, HDR on a real screen, Umbra's"
  echo "encoding and the stream itself, games, and other $compositor versions."
}

series_status() {
  case "$component-$series" in
    kwin-6.6) echo "The KWin 6.6 patch has run on hardware on Ubuntu 26.04 (KWin 6.6.6)." ;;
    mutter-50) echo "The Mutter 50 patch has run on hardware on Ubuntu 26.04 (Mutter 50.1), in revision 1; these packages carry revision 2 (two memory-leak fixes)." ;;
    kwin-6.7) echo "The KWin 6.7 patch has not run on hardware anywhere yet." ;;
    mutter-51) echo "The Mutter 51 patch has not run on hardware anywhere yet." ;;
    *) echo "The $compositor $series patch has not run on hardware anywhere yet." ;;
  esac
}

. /etc/os-release
distro_name="$NAME${VERSION_ID:+ $VERSION_ID}"
vendor=${NAME%% *}

# "An Ubuntu", "A Fedora".
An() { case $1 in [AEIOUaeiou]*) echo "An $1" ;; *) echo "A $1" ;; esac; }

# Defaults a family may override: whose packages these are, and which installed versions fit.
fam_repo_label() { echo "$vendor's"; }
fam_vendor() { echo "$vendor"; }
accepted_versions=
source_desc=

# shellcheck source=/dev/null
. "$here/lib/$family.sh"

log "$target ($distro_name): $component, $family packaging"
fam_setup

# Sets: upstream_version (6.7.5), distro_version (as the package manager prints it) and prepares
# the source in $work for fam_build.
fam_fetch
accepted_versions=${accepted_versions:-$distro_version}
source_desc=${source_desc:-"$(fam_repo_label) $component $distro_version source package"}
series=$(series_of "$upstream_version")
patch="$patch_dir/$component-virtual-hdr-screencast-$series.patch"
[ -f "$patch" ] || die "no patch for $compositor $upstream_version: $patch does not exist (port one, see README.md)"
revision=$(awk -v s="$component-$series" '$1 == s { print $2 }' "$here/revisions")
[ -n "$revision" ] || die "no revision for $component-$series in revisions"
log "$compositor $upstream_version ($distro_version): $(basename "$patch"), Umbra revision $revision"

# Sets: built_version, and fills $work/bin (binary packages) and $work/src (source package).
mkdir -p "$work/bin" "$work/src"
fam_build "$patch" "$revision"

log "built packages:"
ls -1 "$work/bin" >&2

# Ship only the packages a desktop already has installed; fam_select prints their file names.
mapfile -t shipped < <(fam_select)
[ "${#shipped[@]}" -gt 0 ] || die "no packages selected from $work/bin"

# The patched code must be in what ships: the compositor library, and for KWin the screencast plugin.
lib_package=
plugin_path=
for f in "${shipped[@]}"; do
  files=$(fam_list_files "$work/bin/$f")
  case $component in
    kwin)
      grep -qE '/libkwin\.so\.[0-9]' <<<"$files" && lib_package=$f
      p=$(grep -E '/kwin/plugins/screencast\.so$' <<<"$files" | head -n1 || true)
      p=${p#./}
      [ -n "$p" ] && plugin_path=/${p#/}
      ;;
    mutter)
      grep -qE '/libmutter-[0-9]+\.so\.[0-9]' <<<"$files" && lib_package=$f
      ;;
  esac
done
[ -n "$lib_package" ] || die "the selected packages do not contain the $compositor library"
if [ "$component" = kwin ]; then
  [ -n "$plugin_path" ] || die "the selected packages do not contain KWin's screencast plugin"
fi
lib_name=$(fam_package_name "$work/bin/$lib_package")

names=()
for f in "${shipped[@]}"; do names+=("$(fam_package_name "$work/bin/$f")"); done

# A distribution that has a target's packages from another source (for example CachyOS's own
# rebuilds) has to say so; fam_check_versions compares what was built against the repository.
fam_check_versions "${names[@]}"

name="Umbra-HDR-${desktop^^}-$target"
dir="$work/$name"
mkdir -p "$dir"
for f in "${shipped[@]}"; do cp "$work/bin/$f" "$dir/"; done

# README.txt: same sections as the Ubuntu 26.04 extra of Umbra 0.5.1.
{
  title="Umbra HDR extra for $desktop_name - $distro_name ($compositor $upstream_version)"
  echo "$title"
  printf '%*s\n' "${#title}" '' | tr ' ' '='
  echo
  echo "WHAT IT IS"
  if [ "$component" = kwin ]; then
    cat <<EOF
Patched KWin packages. They let KWin make the virtual display Umbra creates for each stream HDR,
and capture it at 10 bits. Without them, every KDE stream and co-op pane from Umbra is SDR.
EOF
  else
    cat <<EOF
Patched Mutter packages. They let GNOME switch the virtual monitor Umbra creates for each stream
to HDR, and capture it at 10 bits. Without them, Umbra's GNOME virtual monitors are SDR (a physical
monitor in HDR mode still streams HDR).
EOF
  fi
  echo "Umbra detects the patch by itself; nothing else on the desktop changes."
  echo
  echo "ONLY FOR"
  echo "$distro_name, $desktop_name on Wayland, with $(fam_repo_label) $component $distro_version. Check first:"
  echo "    $(fam_query_cmd "$lib_name")"
  echo "must print $accepted_versions. With any other $compositor version, do not install these packages."
  echo
  if [ "$component" = kwin ] && [ "$series" != 6.6 ]; then
    # KWin 6.7 announces its outputs only through kde_output_device_registry_v2 (plasma-wayland-
    # protocols 1.21) and reports custom modes at their libxcvt refresh rate; Umbra 0.5.2 handles both.
    echo "REQUIRES"
    echo "Umbra 0.5.2 or newer. KWin 6.7 announces its outputs only through a newer protocol than"
    echo "Umbra 0.5.1 knows, so Umbra 0.5.1 finds no KDE output to control: on KDE Plasma 6.7 it streams"
    echo "the desktop without a virtual display per stream, co-op or HDR, with or without these packages."
    echo
  fi
  echo "PACKAGES (version $built_version)"
  (IFS=,; echo "${names[*]}" | sed 's/,/, /g')
  echo
  echo "INSTALL"
  echo "1. Check the files:"
  echo "       sha256sum -c SHA256SUMS"
  echo "2. Install:"
  echo "       $(fam_install_cmd "${shipped[@]}")"
  echo "3. Log out and back in ($compositor is the desktop's compositor)."
  echo "4. Make Umbra check the desktop again: quit it, clear its remembered answer, and start it again"
  echo "   from the application menu:"
  echo "       flatpak kill io.github.PreceptorOfMagic.Umbra"
  echo "       rm -f ~/.var/app/io.github.PreceptorOfMagic.Umbra/config/sunshine/$umbra_cache"
  echo
  echo "VERIFY"
  if [ "$component" = kwin ]; then
    echo "- The patched plugin is installed (prints 1):"
    echo "      grep -c \"offering HDR\" $plugin_path"
  fi
  if declare -f fam_verify_note >/dev/null; then fam_verify_note "$lib_name"; fi
  echo "- Umbra offers HDR on its virtual displays (prints <UmbraCoopHdr>1):"
  echo "      curl -s http://localhost:47989/serverinfo | grep -o '<UmbraCoopHdr>[01]'"
  echo "  and its log says \"${desktop_name%% *} virtual displays offer HDR\":"
  echo "      grep \"virtual displays\" ~/.var/app/io.github.PreceptorOfMagic.Umbra/config/sunshine/sunshine.log"
  echo "Then turn HDR on for this host in Eclipse."
  echo
  echo "UPDATES"
  fam_updates_note
  echo "Do not reinstall these packages over a newer $compositor; check the Umbra releases for an HDR"
  echo "extra that matches it."
  echo
  echo "ROLLBACK"
  echo "    $(fam_rollback_cmd "${names[@]}")"
  echo "then log out and back in."
  echo
  echo "SOURCE AND LICENCE"
  echo "Built from $source_desc with one added patch,"
  echo "$(basename "$patch"):"
  echo "    $umbra_tree/$component-virtual-hdr"
  echo "The complete source is $name-source.tar.gz in the same release."
  echo "$compositor's licences are in each package (for example /usr/share/licenses or"
  echo "/usr/share/doc/<package>/copyright after installing)."
  echo "$compositor is $upstream_org's project; these packages are not made or supported by"
  echo "$upstream_org or $(fam_vendor)."
  echo
  echo "TESTED"
  tested_note
} > "$dir/README.txt"

(cd "$dir" && sha256sum -- "${shipped[@]}" > SHA256SUMS)

# Reproducible archives: fixed order, owner and times.
pack() { # <archive> <directory>
  tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2026-01-01 00:00Z' \
    -C "$(dirname "$2")" -cf - "$(basename "$2")" | gzip -n -9 > "$1"
  (cd "$(dirname "$1")" && sha256sum "$(basename "$1")" > "$(basename "$1").sha256")
}

src_dir="$work/$name-source"
mkdir -p "$src_dir"
cp -a "$work/src/." "$src_dir/"
cp "$patch" "$src_dir/"
cat > "$src_dir/README.txt" <<EOF
Source of $name.tar.gz: $source_desc, plus
$(basename "$patch") from $umbra_tree/$component-virtual-hdr
(already integrated into the packaging below, as version $built_version).
EOF

pack "$out/$name.tar.gz" "$dir"
pack "$out/$name-source.tar.gz" "$src_dir"
log "wrote $out/$name.tar.gz and $out/$name-source.tar.gz"
cat "$dir/README.txt" >&2
}

main "$@"
