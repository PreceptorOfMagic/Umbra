# Wayland color management protocol

color-management-v1.xml is the unmodified wayland-protocols1.48 file supplied by
the Freedesktop25.08 SDK used for the source build. Its copyright/license notices
are embedded in the XML. Only version1 is bound at runtime; version2 events are
not requested. Upstream: https://gitlab.freedesktop.org/wayland/wayland-protocols

SHA256: b05c15a021d467cff446ab88d8a41d3a908a66cdb5ae104611a410007a01979b

Vendored separately so the older upstream Apollo protocol submodule can remain
unchanged. This interface is optional at runtime: missing or unsupported output
color information retains SDR capture.
