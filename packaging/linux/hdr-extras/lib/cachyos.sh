# CachyOS. KWin: CachyOS has no recipe of its own; its x86-64-v3, v4 and Zen 4 repositories carry
# Arch's PKGBUILD rebuilt as <Arch version>.1, and its generic repositories use Arch's package. This
# builds Arch's PKGBUILD at CachyOS's version, so one package fits every CachyOS repository set.
# Mutter: CachyOS's own recipe (CachyOS-PKGBUILDS, with its own patches), which its [cachyos]
# repository puts ahead of Arch's. Built in the cachyos-v3 image (an x86-64-v3 CPU runs it).
# Sourced by build.sh.
. "$here/lib/pacman.sh"

cachyos_extra_dbs=(
  https://mirror.cachyos.org/repo/x86_64_v4/cachyos-extra-v4/cachyos-extra-v4.db
  https://mirror.cachyos.org/repo/x86_64_v4/cachyos-extra-znver4/cachyos-extra-znver4.db
)

fam_repo_label() { echo "CachyOS's"; }

fam_fetch() {
  distro_version=$(pacman_repo_version "$component")
  [ -n "$distro_version" ] || die "pacman knows no $component"
  case $component in
    kwin)
      local arch_version
      arch_version=$(pacman_repo_version kwin extra)
      [ "${distro_version%.*}" = "$arch_version" ] ||
        die "CachyOS's kwin $distro_version is not a rebuild of Arch's $arch_version"
      git clone -q --depth 1 --branch "${arch_version/:/-}" \
        https://gitlab.archlinux.org/archlinux/packaging/packages/kwin.git "$work/recipe"
      accepted_versions="$distro_version (CachyOS's x86-64-v3, v4 and Zen 4 repositories) or $arch_version
(its generic x86-64 repositories, which take KWin from Arch)"
      source_desc="Arch's kwin $arch_version package source (which CachyOS rebuilds as $distro_version)"
      ;;
    mutter)
      [ -n "$(pacman_repo_version mutter cachyos)" ] || die "CachyOS's [cachyos] repository has no mutter"
      git clone -q --depth 1 https://github.com/CachyOS/CachyOS-PKGBUILDS "$work/cachyos-pkgbuilds"
      cp -a "$work/cachyos-pkgbuilds/mutter" "$work/recipe"
      local recipe_version
      recipe_version=$(. "$work/recipe/PKGBUILD" >/dev/null 2>&1; echo "${epoch:+$epoch:}$pkgver-$pkgrel")
      [ "$recipe_version" = "$distro_version" ] ||
        die "CachyOS-PKGBUILDS has mutter $recipe_version, but CachyOS's repository has $distro_version"
      source_desc="CachyOS's mutter $distro_version package source (CachyOS-PKGBUILDS)"
      ;;
  esac
  upstream_version=$(. "$work/recipe/PKGBUILD" >/dev/null 2>&1; echo "$pkgver")
}

fam_build() {
  local pkgrel=
  [ "$component" = kwin ] && pkgrel=${distro_version##*-}
  pacman_build "$work/recipe" "$1" "$pkgrel"
  [ "$built_version" = "$distro_version" ] || die "built $built_version, but CachyOS has $distro_version"
}

fam_check_versions() {
  local n db v
  for n in "$@"; do
    [ "$(pacman_repo_version "$n")" = "$distro_version" ] ||
      die "CachyOS's $n is $(pacman_repo_version "$n"), not $distro_version"
  done
  [ "$component" = kwin ] || return 0
  # The v3 repositories are configured here; v4 and Zen 4 must carry the same rebuild.
  for db in "${cachyos_extra_dbs[@]}"; do
    curl -fsSL -o "$work/extra.db" "$db"
    v=$(bsdtar -tf "$work/extra.db" | sed -n 's|^kwin-\([^/]*\)/$|\1|p' | head -n1)
    [ "$v" = "$distro_version" ] || die "$(basename "$db") has kwin $v, not $distro_version"
  done
}

fam_updates_note() {
  echo "These packages have the version of CachyOS's own build, so pacman installs them over it and"
  echo "keeps them until CachyOS ships a newer $compositor; that update replaces them, and streams are"
  echo "SDR again (Umbra notices at the next stream)."
  if [ "$component" = kwin ]; then
    echo "On CachyOS's generic x86-64 repositories they are one sub-release above Arch's KWin, and"
    echo "Arch's next KWin replaces them the same way."
  fi
}
