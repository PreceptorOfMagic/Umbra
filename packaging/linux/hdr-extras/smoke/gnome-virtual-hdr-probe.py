#!/usr/bin/env python3
"""Ask a running Mutter for a virtual monitor the way Umbra does, then report its HDR support.

ScreenCast.CreateSession + RecordVirtual (with a mode, so the monitor exists without a consumer)
creates the virtual monitor. The probe then reads its DisplayConfig "supported-color-modes",
switches it to BT.2100 (color-mode 1) if offered, and lists the video formats its PipeWire stream
advertises (pw-dump). Prints one JSON object. With the HDR patch: modes [0, 1], color-mode 1
applied, 10-bit formats offered. Stock Mutter: modes [0], nothing to switch, 8-bit formats only.
"""
import json
import sys
import time

from gi.repository import Gio, GLib

from pw_formats import TEN_BIT, stream_formats

BUS = Gio.bus_get_sync(Gio.BusType.SESSION)


def call(name, path, iface, method, args, reply):
    return BUS.call_sync(name, path, iface, method, args, GLib.VariantType(reply) if reply else None,
                         Gio.DBusCallFlags.NONE, 10000, None).unpack()


def pump(seconds):
    ctx = GLib.MainContext.default()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ctx.iteration(False)
        time.sleep(0.02)


def display_state():
    return call("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                "org.gnome.Mutter.DisplayConfig", "GetCurrentState", None,
                "(ua((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv})")


def virtual_monitor(state):
    for (spec, modes, props) in state[1]:
        if spec[0].startswith("Meta-"):
            return spec, modes, props
    return None


def main():
    result = {"virtual_monitor": None, "supported_color_modes": None, "color_mode_applied": None,
              "formats": [], "ten_bit_formats": [], "errors": []}
    node = {}

    session_path = call("org.gnome.Mutter.ScreenCast", "/org/gnome/Mutter/ScreenCast",
                        "org.gnome.Mutter.ScreenCast", "CreateSession",
                        GLib.Variant("(a{sv})", ({},)), "(o)")[0]
    mode = {"size": GLib.Variant("(uu)", (1280, 720)), "refresh-rate": GLib.Variant("d", 60.0),
            "is-preferred": GLib.Variant("b", True)}
    stream_path = call("org.gnome.Mutter.ScreenCast", session_path,
                       "org.gnome.Mutter.ScreenCast.Session", "RecordVirtual",
                       GLib.Variant("(a{sv})", ({"cursor-mode": GLib.Variant("u", 1),
                                                 "modes": GLib.Variant("aa{sv}", [mode])},)), "(o)")[0]
    BUS.signal_subscribe("org.gnome.Mutter.ScreenCast", "org.gnome.Mutter.ScreenCast.Stream",
                         "PipeWireStreamAdded", stream_path, None, Gio.DBusSignalFlags.NONE,
                         lambda *a: node.setdefault("id", a[5].unpack()[0]))
    call("org.gnome.Mutter.ScreenCast", session_path, "org.gnome.Mutter.ScreenCast.Session",
         "Start", None, None)
    pump(3)

    state = display_state()
    monitor = virtual_monitor(state)
    if not monitor:
        result["errors"].append("no virtual monitor after RecordVirtual")
        print(json.dumps(result))
        return 1
    spec, modes, props = monitor
    result["virtual_monitor"] = spec[0]
    result["supported_color_modes"] = list(props.get("supported-color-modes", []))
    result["node_id"] = node.get("id")

    if 1 in result["supported_color_modes"]:
        current = next(m[0] for m in modes if m[6].get("is-current"))
        logical = []
        for (x, y, scale, transform, primary, monitors, _props) in state[2]:
            entries = []
            for m in monitors:
                entry = {"color-mode": GLib.Variant("u", 1)} if m[0] == spec[0] else {}
                mode_id = current if m[0] == spec[0] else next(
                    mm[0] for (s, mms, _) in state[1] if s[0] == m[0] for mm in mms if mm[6].get("is-current"))
                entries.append((m[0], mode_id, entry))
            logical.append((x, y, scale, transform, primary, entries))
        try:
            call("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                 "org.gnome.Mutter.DisplayConfig", "ApplyMonitorsConfig",
                 GLib.Variant("(uua(iiduba(ssa{sv}))a{sv})", (state[0], 1, logical, {})), None)
        except GLib.Error as e:
            result["errors"].append("ApplyMonitorsConfig: " + e.message)
        pump(3)
        props = virtual_monitor(display_state())[2]
        result["color_mode_applied"] = props.get("color-mode")

    if node.get("id") is not None:
        result["formats"] = stream_formats(node["id"])
        result["ten_bit_formats"] = sorted(TEN_BIT & set(result["formats"]))
    else:
        result["errors"].append("no PipeWire node for the stream")
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
