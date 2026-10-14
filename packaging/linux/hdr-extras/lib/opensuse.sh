# openSUSE Tumbleweed: zypper source-install (the source RPM and its build dependencies), rpmbuild.
# openSUSE's specs say Release: 0 (its build service numbers releases), so Release becomes the
# repository's binary release plus .umbrahdrN. Sourced by build.sh.
. "$here/lib/rpm.sh"

opensuse_package() { # the source package name
  case $component in
    kwin) echo kwin6 ;;
    mutter) echo mutter ;;
  esac
}

opensuse_evr() { # <package>: the version-release zypper would install
  zypper -n -q info "$1" 2>/dev/null | sed -n 's/^Version *: //p' | head -n1
}

fam_repo_label() { echo "openSUSE's"; }

fam_setup() {
  zypper -n -q install rpm-build patch >/dev/null
  if ! zypper -n lr -u | grep -q '/source/tumbleweed/'; then
    zypper -n -q addrepo -f https://download.opensuse.org/source/tumbleweed/repo/oss/ umbra-source
  fi
  zypper -n -q --gpg-auto-import-keys refresh >/dev/null
}

fam_fetch() {
  local pkg spec_version
  pkg=$(opensuse_package)
  distro_version=$(opensuse_evr "$pkg")
  [ -n "$distro_version" ] || die "zypper knows no $pkg"
  upstream_version=${distro_version%-*}
  zypper -n -q source-install "$pkg" >/dev/null
  spec="$rpm_topdir/SPECS/$pkg.spec"
  [ -f "$spec" ] || die "the $pkg source RPM has no $pkg.spec"
  spec_version=$(rpmspec -q --srpm --qf '%{VERSION}' "$spec" 2>/dev/null)
  [ "$spec_version" = "$upstream_version" ] ||
    die "source $pkg $spec_version is not the repository's $distro_version"
}

fam_build() {
  local patch=$1 rev=$2 release
  release="${distro_version##*-}.umbrahdr$rev"
  rpm_prepare_spec "$spec" "$patch" "$release"
  rpm_build "$spec"
  built_version="$upstream_version-$release"
}

fam_check_versions() {
  local n
  for n in "$@"; do
    [ "$(opensuse_evr "$n")" = "$distro_version" ] ||
      die "openSUSE's $n is $(opensuse_evr "$n"), not $distro_version"
  done
}

fam_install_cmd() {
  local cmd="sudo zypper install --allow-unsigned-rpm" f
  for f in "$@"; do cmd+=" ./$f"; done
  echo "$cmd"
}

fam_rollback_cmd() {
  local cmd="sudo zypper install --oldpackage" n
  for n in "$@"; do cmd+=" $n=$distro_version"; done
  echo "$cmd"
}

fam_updates_note() {
  echo "Tumbleweed's update (zypper dup) puts openSUSE's own $compositor back, at the latest when openSUSE"
  echo "ships a new one, and streams are SDR again (Umbra notices at the next stream)."
}
