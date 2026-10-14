# Shared by arch.sh and cachyos.sh: a PKGBUILD with the patch applied after the distribution's own
# prepare(), built by makepkg for any x86-64 CPU. The package keeps the repository's version:
# pacman has no version slot between two repository releases for a third party (CachyOS already
# uses Arch's sub-release for its rebuilds), so `pacman -U` reinstalls over the repository's
# package and the distribution's next update replaces it. Sourced through the family file.

pacman_marker() { # a file only the compositor's own source tree has
  case $component in
    kwin) echo src/plugins/screencast/screencaststream.cpp ;;
    mutter) echo src/backends/native/meta-output-virtual.c ;;
  esac
}

pacman_repo_version() { # <package> [repository]: the version pacman would install
  if [ -n "${2:-}" ]; then
    pacman -Sl "$2" | awk -v p="$1" '$2 == p { print $3 }'
  else
    pacman -Si "$1" | sed -n 's/^Version *: //p' | head -n1
  fi
}

fam_setup() {
  pacman -Syu --noconfirm --needed base-devel git sudo >/dev/null
  id builder >/dev/null 2>&1 || useradd -m builder
  echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder
  # Arch's own flags (CachyOS's image builds for the build machine's CPU): the package must run on
  # every x86-64 machine, whichever CachyOS repositories it uses.
  mkdir -p /etc/makepkg.conf.d
  cat > /etc/makepkg.conf.d/umbra-hdr.conf <<'EOF'
CFLAGS="-march=x86-64 -mtune=generic -O2 -pipe -fno-plt -fexceptions \
        -Wp,-D_FORTIFY_SOURCE=3 -Wformat -Werror=format-security \
        -fstack-clash-protection -fcf-protection \
        -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
CXXFLAGS="$CFLAGS -Wp,-D_GLIBCXX_ASSERTIONS"
LDFLAGS="-Wl,-O1 -Wl,--sort-common -Wl,--as-needed -Wl,-z,relro -Wl,-z,now \
         -Wl,-z,pack-relative-relocs"
LTOFLAGS="-flto=auto"
OPTIONS=(strip docs !libtool !staticlibs emptydirs zipman purge !debug lto)
PKGEXT='.pkg.tar.zst'
EOF
}

# <recipe directory> <patch> <pkgrel or empty>: append the patch to the PKGBUILD and run makepkg.
pacman_build() {
  local dir=$1 patch=$2 pkgrel=${3:-}
  cp "$patch" "$dir/umbra-virtual-hdr-screencast.patch"
  cat >> "$dir/PKGBUILD" <<EOF

# --- Umbra HDR extra: $(basename "$patch"), applied after the distribution's own prepare() ---
source+=("umbra-virtual-hdr-screencast.patch")
for _umbra_sums in md5sums sha1sums sha224sums sha256sums sha384sums sha512sums b2sums cksums; do
  if declare -p "\$_umbra_sums" &>/dev/null; then eval "\$_umbra_sums+=('SKIP')"; fi
done
if declare -f prepare >/dev/null; then eval "_umbra_distro_\$(declare -f prepare)"; fi
prepare() {
  if declare -f _umbra_distro_prepare >/dev/null; then _umbra_distro_prepare; fi
  local _d
  for _d in "\$srcdir"/*/; do
    if [[ -e "\$_d/$(pacman_marker)" ]]; then
      patch -d "\$_d" -Np1 -i "\$srcdir/umbra-virtual-hdr-screencast.patch"
      return
    fi
  done
  echo "Umbra: no $component source tree in \$srcdir" >&2
  return 1
}
${pkgrel:+pkgrel=$pkgrel}
EOF
  chown -R builder: "$dir"
  if [ -d "$dir/keys/pgp" ]; then
    sudo -u builder gpg --batch --import "$dir"/keys/pgp/*.asc
  fi
  (
    cd "$dir"
    sudo -u builder env PACKAGER="Umbra HDR extra <271783941+PreceptorOfMagic@users.noreply.github.com>" \
      MAKEFLAGS="-j$(nproc)" makepkg --syncdeps --noconfirm --nocheck
    sudo -u builder makepkg --allsource --noconfirm
  )
  # shellcheck disable=SC2046
  built_version=$(. "$dir/PKGBUILD" >/dev/null 2>&1; echo "${epoch:+$epoch:}$pkgver-$pkgrel")
  mv "$dir"/*.pkg.tar.zst "$work/bin/"
  mv "$dir"/*.src.tar.gz "$work/src/"
}

fam_select() {
  ls -1 "$work/bin" | grep -E "^$component-[0-9]"
}

fam_list_files() { bsdtar -tf "$1"; }
fam_package_name() { bsdtar -xOf "$1" .PKGINFO | sed -n 's/^pkgname = //p'; }

fam_query_cmd() { echo "pacman -Q $1 | cut -d' ' -f2"; }

fam_install_cmd() {
  local cmd="sudo pacman -U" f
  for f in "$@"; do cmd+=" $f"; done
  echo "$cmd"
}

fam_rollback_cmd() {
  echo "sudo pacman -S $*"
}

fam_verify_note() {
  echo "- The installed $component is this extra (prints \"Packager : Umbra HDR extra ...\"):"
  echo "      pacman -Qi $1 | grep Packager"
}
