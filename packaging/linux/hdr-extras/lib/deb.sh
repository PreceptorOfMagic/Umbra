# Debian and Ubuntu: the archive's source package, the patch added to debian/patches, the version
# raised to <version>+umbrahdrN (above the archive's, below its next update), dpkg-buildpackage.
# Sourced by build.sh.

deb_patch_name() {
  case $component in
    kwin) echo umbra-virtual-output-hdr-screencast.patch ;;
    mutter) echo umbra-virtual-monitor-hdr-screencast.patch ;;
  esac
}

# The packages a desktop with this compositor has installed (the others are -dev, tests and the
# stand-alone mutter binary).
deb_shipped_regex() {
  case $component in
    kwin) echo '^(kwin-common|kwin-data|kwin-wayland|libkwin[0-9]+)_' ;;
    mutter) echo '^(gir1\.2-mutter-[0-9]+|libmutter-[0-9]+-0|mutter-common|mutter-common-bin)_' ;;
  esac
}

deb_candidate() { apt-cache policy "$1" | awk '/Candidate:/ { print $2 }'; }

fam_setup() {
  export DEBIAN_FRONTEND=noninteractive
  echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80umbra-retries
  local f
  for f in /etc/apt/sources.list.d/*.sources; do
    [ -f "$f" ] && sed -i 's/^Types: deb$/Types: deb deb-src/' "$f"
  done
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends \
    build-essential ca-certificates devscripts dpkg-dev equivs fakeroot quilt >/dev/null
}

fam_fetch() {
  (cd "$work" && apt-get source -qq "$component")
  srcdir=$(find "$work" -mindepth 1 -maxdepth 1 -type d -name "$component-*" | head -n1)
  [ -n "$srcdir" ] || die "apt-get source $component unpacked nothing"
  distro_version=$(dpkg-parsechangelog -l "$srcdir/debian/changelog" -SVersion)
  upstream_version=${distro_version#*:}
  upstream_version=${upstream_version%-*}
  local probe=kwin-wayland
  [ "$component" = mutter ] && probe=mutter-common
  [ "$(deb_candidate "$probe")" = "$distro_version" ] ||
    die "source $component $distro_version is not the archive's $probe $(deb_candidate "$probe")"
  grep -qx '3.0 (quilt)' "$srcdir/debian/source/format" || die "$component is not a 3.0 (quilt) source package"
}

fam_build() {
  local patch=$1 rev=$2 name
  name=$(deb_patch_name)
  built_version="$distro_version+umbrahdr$rev"
  cp "$patch" "$srcdir/debian/patches/$name"
  echo "$name" >> "$srcdir/debian/patches/series"
  (
    cd "$srcdir"
    DEBFULLNAME=${DEBFULLNAME:-PreceptorOfMagic} \
    DEBEMAIL=${DEBEMAIL:-271783941+PreceptorOfMagic@users.noreply.github.com} \
      dch -v "$built_version" --distribution "$VERSION_CODENAME" --force-distribution \
      "Umbra: $(basename "$patch") (Umbra HDR extra)."
    mk-build-deps -i -r -t 'apt-get -y -qq --no-install-recommends' debian/control >/dev/null
    rm -f ./*-build-deps_*
    dpkg-buildpackage -S -d -uc -us -nc >/dev/null
    DEB_BUILD_OPTIONS="nocheck nodoc noautodbgsym parallel=$(nproc)" dpkg-buildpackage -b -uc -us
  )
  mv "$work"/*.deb "$work/bin/"
  local v=${built_version#*:}
  mv "$work/${component}_$v.dsc" "$work/${component}_$v.debian.tar."* "$work/src/"
  mv "$work/${component}_${upstream_version}.orig.tar."* "$work/src/"
}

fam_select() {
  ls -1 "$work/bin" | grep -E "$(deb_shipped_regex)"
}

fam_list_files() { dpkg-deb -c "$1" | awk '{ print $6 }'; }
fam_package_name() { dpkg-deb -f "$1" Package; }

fam_check_versions() {
  local n
  for n in "$@"; do
    [ "$(deb_candidate "$n")" = "$distro_version" ] ||
      die "the archive's $n is $(deb_candidate "$n"), not $distro_version"
  done
}

fam_query_cmd() { echo "dpkg-query -W -f='\${Version}\\n' $1"; }

fam_install_cmd() {
  local cmd="sudo dpkg -i" f
  for f in "$@"; do cmd+=" $f"; done
  echo "$cmd"
}

fam_rollback_cmd() {
  local cmd="sudo apt install --allow-downgrades" n
  for n in "$@"; do cmd+=" $n=$distro_version"; done
  echo "$cmd"
}

fam_updates_note() {
  echo "$(An "$vendor") update of $compositor replaces these packages, and streams are SDR again (Umbra"
  echo "notices at the next stream)."
}
