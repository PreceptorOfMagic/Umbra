# Fedora: dnf download --source, dnf builddep, rpmbuild. Sourced by build.sh.
. "$here/lib/rpm.sh"

fedora_evr() { # <package>: the version-release dnf would install
  dnf -q repoquery --latest-limit=1 --arch=x86_64,noarch --qf '%{version}-%{release}\n' "$1" 2>/dev/null | head -n1
}

fam_setup() {
  dnf -y -q install rpm-build gawk tar gzip findutils 'dnf-command(builddep)' 'dnf-command(download)' >/dev/null
}

fam_fetch() {
  (cd "$work" && dnf -q download --source "$component")
  local srpm srpm_version
  srpm=$(ls "$work"/"$component"-*.src.rpm)
  distro_version=$(fedora_evr "$component")
  [ -n "$distro_version" ] || die "dnf knows no $component"
  srpm_version=$(rpm -qp --qf '%{VERSION}-%{RELEASE}' "$srpm" 2>/dev/null)
  [ "$srpm_version" = "$distro_version" ] ||
    die "source RPM $component $srpm_version is not the repository's $distro_version"
  upstream_version=${distro_version%-*}
  rpm -i "$srpm"
  rm -f "$srpm"
  spec="$rpm_topdir/SPECS/$component.spec"
  [ -f "$spec" ] || die "$component's source RPM has no $component.spec"
  dnf -y -q builddep "$spec" >/dev/null
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
    [ "$(fedora_evr "$n")" = "$distro_version" ] ||
      die "Fedora's $n is $(fedora_evr "$n"), not $distro_version"
  done
}

fam_install_cmd() {
  local cmd="sudo dnf install" f
  for f in "$@"; do cmd+=" ./$f"; done
  echo "$cmd"
}

fam_rollback_cmd() { echo "sudo dnf distro-sync $*"; }

fam_updates_note() {
  echo "A Fedora update of $compositor (dnf upgrade) replaces these packages, and streams are SDR again"
  echo "(Umbra notices at the next stream)."
}
