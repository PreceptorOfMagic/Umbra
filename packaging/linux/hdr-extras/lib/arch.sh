# Arch Linux: the PKGBUILD at the tag of the repository's version, from Arch's packaging GitLab.
# Sourced by build.sh.
. "$here/lib/pacman.sh"

fam_fetch() {
  distro_version=$(pacman_repo_version "$component")
  [ -n "$distro_version" ] || die "pacman knows no $component"
  local tag=${distro_version/:/-}
  git clone -q --depth 1 --branch "$tag" \
    "https://gitlab.archlinux.org/archlinux/packaging/packages/$component.git" "$work/recipe"
  upstream_version=$(. "$work/recipe/PKGBUILD" >/dev/null 2>&1; echo "$pkgver")
}

fam_build() {
  pacman_build "$work/recipe" "$1" ""
  [ "$built_version" = "$distro_version" ] || die "built $built_version, but Arch has $distro_version"
}

fam_check_versions() {
  local n
  for n in "$@"; do
    [ "$(pacman_repo_version "$n")" = "$distro_version" ] ||
      die "Arch's $n is $(pacman_repo_version "$n"), not $distro_version"
  done
}

fam_updates_note() {
  echo "These packages have the version of Arch's own, so pacman installs them over Arch's and keeps"
  echo "them until Arch ships a newer $compositor; that update replaces them, and streams are SDR again"
  echo "(Umbra notices at the next stream)."
}
