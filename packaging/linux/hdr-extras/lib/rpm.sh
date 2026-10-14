# Shared by fedora.sh and opensuse.sh: the distribution's source RPM, its spec given the patch as
# an extra source applied at the end of %prep (after the distribution's own patches), and Release
# set to the repository's release plus .umbrahdrN (above it, below the distribution's next update).
# Sourced through the family file.

rpm_topdir=$(rpm --eval '%{_topdir}' 2>/dev/null || echo /root/rpmbuild)

# <spec> <patch> <release>
rpm_prepare_spec() {
  local spec=$1 patch=$2 release=$3
  cp "$patch" "$rpm_topdir/SOURCES/umbra-virtual-hdr-screencast.patch"
  awk -v release="$release" '
    !done_release && /^Release:/ {
      print "Release:        " release
      print "Source9990:     umbra-virtual-hdr-screencast.patch"
      done_release = 1
      next
    }
    /^%prep/ { in_prep = 1; print; next }
    in_prep && /^%(conf|build|generate_buildrequires|install|check)([[:space:]]|$)/ {
      print "# Umbra HDR extra"
      print "patch -p1 -i %{SOURCE9990}"
      print ""
      in_prep = 0
      done_prep = 1
    }
    { print }
    END { if (!done_release || !done_prep) exit 1 }
  ' "$spec" > "$spec.umbra" || die "could not add the patch to $spec (no Release: tag or %prep section)"
  mv "$spec.umbra" "$spec"
}

# <spec>: build binary and source packages; the distribution's tests and debuginfo are skipped.
rpm_build() {
  rpmbuild -ba --nocheck --define 'debug_package %{nil}' --define "_smp_mflags -j$(nproc)" "$1"
  find "$rpm_topdir/RPMS" -name '*.rpm' -exec mv {} "$work/bin/" \;
  find "$rpm_topdir/SRPMS" -name '*.src.rpm' -exec mv {} "$work/src/" \;
}

# Ship what a desktop has installed: not -devel, -doc, -lang, tests or debug packages.
fam_select() {
  ls -1 "$work/bin" | grep -vE -- '-(devel|doc|docs|lang|tests?|debuginfo|debugsource)-[0-9]'
}

fam_list_files() { rpm -qlp "$1" 2>/dev/null; }
fam_package_name() { rpm -qp --qf '%{NAME}' "$1" 2>/dev/null; }

fam_query_cmd() { echo "rpm -q --qf '%{VERSION}-%{RELEASE}\\n' $1"; }
