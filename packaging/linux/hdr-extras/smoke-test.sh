#!/bin/bash
# Smoke-test one built HDR extra in a fresh container of its target distribution, as root:
#
#   smoke-test.sh <kde|gnome> <target> <directory with Umbra-HDR-<KDE|GNOME>-<target>.tar.gz>
#
# 1. Installs the distribution's own compositor packages, as a desktop has them.
# 2. Follows the extra's README.txt, running each command as it is printed there (without sudo,
#    answering yes): the version check, sha256sum -c, the install, its VERIFY checks, the rollback.
#    Checks the installed version before, after install and after rollback, and that no file of
#    the installed packages links against a missing library.
# 3. Runtime, stock and then patched, with software rendering and no GPU:
#    GNOME: Mutter headless creates a virtual monitor as Umbra asks for it (ScreenCast
#      RecordVirtual). Patched, it must offer BT.2100, switch to it and offer 10-bit formats;
#      stock must not.
#    KDE: KWin creates a virtual output as Umbra asks for it (zkde_screencast stream_virtual_output).
#      With a KMS device (vkms; the container needs it and --privileged) on KWin's DRM backend:
#      patched must report it HDR-capable and switch it to HDR; stock must not. Without one, KWin
#      runs its headless backend, which has no OpenGL: this only shows that KWin starts and the
#      screencast plugin loads and answers the request.
# Hardware HDR output, encoding and Umbra itself are outside this test.
set -euo pipefail

main() {
desktop=${1:?usage: smoke-test.sh <kde|gnome> <target> <dir>}
target=${2:?}
dir=${3:?}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)

failures=0
pass() { printf 'PASS  %s\n' "$*"; }
fail() { printf 'FAIL  %s\n' "$*"; failures=$((failures + 1)); }
note() { printf 'NOTE  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

family=
while read -r name _image fam _rest; do
  [ "$name" = "$target" ] && { family=$fam; break; }
done < <(grep -v '^#' "$here/targets")
[ -n "$family" ] || die "unknown target $target"
case $desktop in kde) component=kwin ;; gnome) component=mutter ;; *) die "desktop must be kde or gnome" ;; esac

name="Umbra-HDR-${desktop^^}-$target"
[ -f "$dir/$name.tar.gz" ] || die "no $dir/$name.tar.gz"

# Family specifics: install packages, the test tools, and the yes-to-everything form of a command.
case $family in
  deb)
    export DEBIAN_FRONTEND=noninteractive
    echo 'APT::Get::Assume-Yes "true";' > /etc/apt/apt.conf.d/90umbra-smoke
    echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80umbra-retries
    apt-get update -qq
    pkg_install() { apt-get install -y -qq --no-install-recommends "$@" >/dev/null; }
    tools_gnome=(mutter pipewire pipewire-bin python3 python3-gi gir1.2-glib-2.0 dbus-daemon dbus-bin libglib2.0-bin libegl-mesa0 libgl1-mesa-dri)
    tools_kde=(pipewire pipewire-bin python3 dbus-daemon dbus-bin gcc libc6-dev libwayland-dev libwayland-bin libegl-mesa0 libgl1-mesa-dri libgbm1)
    unattended() { sed 's/^dpkg -i /dpkg -i --force-confnew /'; }
    files_of() { dpkg -L "$1"; }
    ;;
  fedora)
    grep -q '^assumeyes' /etc/dnf/dnf.conf 2>/dev/null || echo 'assumeyes=True' >> /etc/dnf/dnf.conf
    pkg_install() { dnf -y -q install "$@" >/dev/null; }
    pkg_install tar gzip
    tools_gnome=(pipewire pipewire-utils python3 python3-gobject dbus-daemon dbus-tools glib2 mesa-dri-drivers mesa-libEGL)
    tools_kde=(pipewire pipewire-utils python3 dbus-daemon dbus-tools gcc wayland-devel mesa-dri-drivers mesa-libEGL mesa-libgbm)
    unattended() { cat; }
    files_of() { rpm -ql "$1"; }
    ;;
  opensuse)
    pkg_install() { zypper -n -q install "$@" >/dev/null; }
    pkg_install tar gzip
    tools_gnome=(pipewire pipewire-tools python3 python3-gobject typelib-1_0-GLib-2_0 typelib-1_0-Gio-2_0 dbus-1 dbus-1-daemon glib2-tools Mesa-dri Mesa-libEGL1)
    # The image installs no recommended packages, so gcc comes without the C library headers.
    tools_kde=(pipewire pipewire-tools python3 dbus-1 dbus-1-daemon gcc glibc-devel wayland-devel Mesa-dri Mesa-libEGL1 libgbm1)
    unattended() { sed 's/^zypper /zypper -n /'; }
    files_of() { rpm -ql "$1"; }
    ;;
  arch | cachyos)
    # CachyOS's pacman runs install scripts and hooks without network access, and a container
    # cannot isolate the network, so it would skip them all (glib-compile-schemas among them,
    # and Mutter aborts without compiled schemas). A desktop system is not affected.
    allow_hooks() {
      if grep -aq DisableSandboxNetwork /usr/bin/pacman /usr/lib/libalpm.so* 2>/dev/null &&
         ! grep -q '^DisableSandboxNetwork' /etc/pacman.conf; then
        sed -i '/^\[options\]/a DisableSandboxNetwork' /etc/pacman.conf
      fi
    }
    allow_hooks
    pacman -Syu --noconfirm >/dev/null
    allow_hooks
    pkg_install() { pacman -S --noconfirm --needed "$@" >/dev/null; }
    tools_gnome=(pipewire python python-gobject dbus glib2 mesa)
    tools_kde=(pipewire python dbus gcc wayland mesa)
    unattended() { sed 's/^pacman \(-[US]\) /pacman \1 --noconfirm /'; }
    files_of() { pacman -Qlq "$1"; }
    ;;
  *) die "no smoke test for family $family" ;;
esac

mkdir -p "$work/x"
tar -xzf "$dir/$name.tar.gz" -C "$work/x"
cd "$work/x/$name"
readme=README.txt

# README.txt fields.
check_cmd=$(sed -n '/Check first:/{n;s/^ *//;p;q}' "$readme")
# The versions between "must print" and "With any other" (one line, or two for CachyOS's KWin).
accepted=$(sed -n '/^must print /{:a;/With any other/!{N;ba};p;q}' "$readme" | tr '\n' ' ' |
  sed 's/^must print //; s/With any other.*//' | grep -oE '[0-9]+(:[0-9]+)?\.[0-9][^ ,()]*[0-9]' | sort -u)
built=$(sed -n 's/^PACKAGES (version \(.*\))$/\1/p' "$readme")
read -r -a packages <<<"$(sed -n '/^PACKAGES (version/{n;s/,//g;p;q}' "$readme")"
install_cmd=$(sed -n '/^2\. Install:/{n;s/^ *//;s/^sudo //;p;q}' "$readme" | unattended)
rollback_cmd=$(sed -n '/^ROLLBACK/{n;s/^ *//;s/^sudo //;p;q}' "$readme" | unattended)
[ -n "$check_cmd" ] && [ -n "$accepted" ] && [ -n "$built" ] && [ "${#packages[@]}" -gt 0 ] &&
  [ -n "$install_cmd" ] && [ -n "$rollback_cmd" ] || die "README.txt lacks a section the test needs"
echo "== $name: ${packages[*]} at $built (for $(echo $accepted))"

installed() { { (eval "$check_cmd") 2>/dev/null || true; } | tail -n1; }
is_accepted() { grep -qxF -- "$1" <<<"$accepted"; }

# 1. The distribution's own packages, plus what the runtime test needs.
tools=("${tools_gnome[@]}")
[ "$desktop" = kde ] && tools=("${tools_kde[@]}")
pkg_install "${packages[@]}"
# Tools one by one: a name a distribution lacks costs only what needed it, and is reported.
for t in "${tools[@]}"; do pkg_install "$t" 2>/dev/null || note "no package $t here"; done
stock=$(installed)
if is_accepted "$stock"; then pass "README check prints the distribution's version ($stock)"; else fail "README check prints '$stock', not one of: $(echo $accepted)"; fi

runtime() { # <label>: prints the probe's JSON
  if [ "$desktop" = gnome ]; then
    { timeout 180 bash "$here/smoke/gnome-runtime.sh" "$work/$1" 2>/dev/null || true; } | tail -n1
  else
    # Decided on the stock run and kept for the patched one: DRM when a KMS device exists and
    # stock KWin starts on it, otherwise the headless backend.
    local mode result
    if [ -f "$work/kde-mode" ]; then
      mode=$(cat "$work/kde-mode")
    else
      mode=virtual
      ls /dev/dri/card* >/dev/null 2>&1 && mode=drm
    fi
    result=$({ timeout 180 bash "$here/smoke/kde-runtime.sh" "$mode" "$work/$1" "$repo" 2>/dev/null || true; } | tail -n1)
    if [ "$mode" = drm ] && [ ! -f "$work/kde-mode" ] && grep -q 'kwin_wayland exited' <<<"$result"; then
      note "KWin did not start on the KMS device ($(grep -m1 -iE 'drm|fail|error' "$work/$1.log" 2>/dev/null || true)); using its headless backend"
      mode=virtual
      result=$({ timeout 180 bash "$here/smoke/kde-runtime.sh" virtual "$work/$1" "$repo" 2>/dev/null || true; } | tail -n1)
    fi
    [ -f "$work/kde-mode" ] || echo "$mode" > "$work/kde-mode"
    echo "$result"
  fi
}
field() { python3 -c 'import json,sys; v=json.loads(sys.argv[1]).get(sys.argv[2]); print(json.dumps(v) if not isinstance(v, str) else v)' "$1" "$2" 2>/dev/null || true; }

# When the compositor did not start or the KDE probe did not compile, their logs say why.
show_log() {
  if grep -q 'exited' <<<"$2"; then
    echo "   --- end of the $1 compositor log:"; tail -n 25 "$work/$1.log" 2>/dev/null | sed 's/^/   | /'
  elif grep -q 'did not compile' <<<"$2"; then
    echo "   --- the probe's compiler output:"; head -n 25 "$work/$1.cc.log" 2>/dev/null | sed 's/^/   | /'
  fi
  true
}

stock_run=$(runtime stock)
echo "   stock runtime: $stock_run"
show_log stock "$stock_run"

# 2. The README's own steps.
if sha256sum -c SHA256SUMS >/dev/null; then pass "sha256sum -c SHA256SUMS"; else fail "sha256sum -c SHA256SUMS"; fi
echo "   install: $install_cmd"
if (eval "$install_cmd") >"$work/install.log" 2>&1; then pass "README install command"; else fail "README install command"; tail -20 "$work/install.log"; fi
now=$(installed)
if [ "$now" = "$built" ]; then pass "installed version is $built"; else fail "installed version is '$now', not $built"; fi

# VERIFY lines that need no running Umbra: the KDE plugin string, pacman's Packager.
while read -r cmd; do
  case $cmd in
    grep\ -c*) [ "$( (eval "$cmd") 2>/dev/null)" = 1 ] && pass "README verify: $cmd" || fail "README verify: $cmd" ;;
    pacman\ -Qi*) (eval "$cmd") 2>/dev/null | grep -q 'Umbra HDR extra' && pass "README verify: $cmd" || fail "README verify: $cmd" ;;
  esac
done < <(sed -n '/^VERIFY/,/^UPDATES/p' "$readme" | sed -n 's/^      //p' | grep -E '^(grep -c|pacman -Qi)')

missing=
for p in "${packages[@]}"; do
  while read -r f; do
    [ -f "$f" ] && [ ! -L "$f" ] && [ "$(head -c4 "$f" 2>/dev/null | tr -d '\0')" = $'\x7fELF' ] || continue
    m=$(ldd "$f" 2>/dev/null | grep 'not found' || true)
    [ -n "$m" ] && missing+="$f: $m"$'\n'
  done < <(files_of "$p")
done
if [ -z "$missing" ]; then pass "every library and program in ${packages[*]} finds its libraries"; else fail "missing libraries:"; printf '%s' "$missing"; fi

# 3. Runtime, patched.
patched_run=$(runtime patched)
echo "   patched runtime: $patched_run"
show_log patched "$patched_run"
if [ "$desktop" = gnome ]; then
  if [ "$(field "$stock_run" supported_color_modes)" = "[0]" ] && [ "$(field "$stock_run" ten_bit_formats)" = "[]" ]; then
    pass "stock Mutter: virtual monitor SDR only, 8-bit formats only"
  else
    fail "stock Mutter: expected SDR only, got $stock_run"
  fi
  if [ "$(field "$patched_run" supported_color_modes)" = "[0, 1]" ] && [ "$(field "$patched_run" color_mode_applied)" = 1 ] &&
     [ "$(field "$patched_run" ten_bit_formats)" != "[]" ] && [ -n "$(field "$patched_run" ten_bit_formats)" ]; then
    pass "patched Mutter: virtual monitor offers BT.2100, switches to it, offers $(field "$patched_run" ten_bit_formats)"
  else
    fail "patched Mutter: expected BT.2100 and 10-bit formats, got $patched_run"
  fi
else
  if [ "$(cat "$work/kde-mode")" = drm ]; then
    # Umbra's output control (kde-output.c from this checkout) must find and resize the output on
    # this KWin, stock and patched alike: every KDE stream depends on it.
    for run in stock patched; do
      r=$stock_run; [ $run = patched ] && r=$patched_run
      if [ "$(field "$r" output_control)" = 1 ] && [ "$(field "$r" resized)" = 1 ]; then
        pass "$run KWin (DRM, vkms): Umbra's output control finds the virtual output and resizes it to 1920x1080"
      else
        fail "$run KWin (DRM, vkms): Umbra's output control: $r"
      fi
    done
    if [ "$(field "$stock_run" hdr_capable)" = 0 ]; then pass "stock KWin (DRM, vkms): virtual output not HDR-capable"; else fail "stock KWin (DRM): expected no HDR, got $stock_run"; fi
    if [ "$(field "$patched_run" hdr_capable)" = 1 ] && [ "$(field "$patched_run" hdr_enabled)" = 1 ]; then
      pass "patched KWin (DRM, vkms): virtual output HDR-capable and switched to HDR"
    else
      fail "patched KWin (DRM): expected HDR, got $patched_run"
    fi
    if [ "$(field "$patched_run" ten_bit_formats)" != "[]" ] && [ -n "$(field "$patched_run" ten_bit_formats)" ]; then
      pass "patched KWin (DRM, vkms): stream offers $(field "$patched_run" ten_bit_formats)"
    elif [ "${UMBRA_SMOKE_REQUIRE_KMS:-}" = 1 ]; then
      fail "patched KWin (DRM, vkms): no 10-bit formats (formats: $(field "$patched_run" formats))"
    else
      note "patched KWin (DRM, vkms): no 10-bit formats; this KMS device may not allocate 10-bit buffers (formats: $(field "$patched_run" formats))"
    fi
  else
    if [ "${UMBRA_SMOKE_REQUIRE_KMS:-}" = 1 ]; then
      fail "KDE runtime test needs a KMS device KWin starts on (UMBRA_SMOKE_REQUIRE_KMS=1)"
    else
      note "no KMS device: KWin's headless backend has no OpenGL, so the HDR check needs vkms"
    fi
    err=$(field "$patched_run" error)
    if [ -n "$(field "$patched_run" node_id)" ] && [ "$(field "$patched_run" node_id)" != null ] || [ "$err" = "stream_virtual_output failed: Unsupported compositing type" ]; then
      pass "patched KWin (headless): starts, loads the screencast plugin, answers Umbra's virtual-output request"
    else
      fail "patched KWin (headless): $patched_run"
    fi
  fi
fi

# 4. The README's rollback.
echo "   rollback: $rollback_cmd"
if (eval "$rollback_cmd") >"$work/rollback.log" 2>&1; then pass "README rollback command"; else fail "README rollback command"; tail -20 "$work/rollback.log"; fi
after=$(installed)
if is_accepted "$after"; then pass "rolled back to the distribution's $after"; else fail "after rollback '$after', not one of: $(echo $accepted)"; fi
# pacman keeps the version, so the rollback shows in the packager instead.
if grep -q 'pacman -Qi' "$readme"; then
  lib=$(sed -n '/pacman -Qi/{s/^ *pacman -Qi \([^ ]*\).*/\1/p;q}' "$readme")
  if pacman -Qi "$lib" | grep -q 'Umbra HDR extra'; then fail "after rollback $lib is still the extra's build"; else pass "after rollback $lib is the repository's build"; fi
fi

echo "== $name: $failures failure(s)"
[ "$failures" = 0 ]
}

main "$@"
