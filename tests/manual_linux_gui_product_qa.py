"""Open the packaged Linux GUI with its real frontend on an isolated X server."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time

from manual_linux_products_qa import request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    folder = Path(tempfile.mkdtemp(prefix="mdo-gui-product-qa-"))
    binary = folder / "mdo"; shutil.copy2(args.binary.resolve(), binary); os.chmod(binary, 0o755)
    home = folder / "mdo-home"
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
    log_path = folder / "run.log"
    log = log_path.open("wb")
    process = subprocess.Popen([str(binary), "--port", str(port)], cwd=folder,
        env={**os.environ, "MDO_HOME": str(home)}, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise AssertionError(log_path.read_text(errors="replace"))
            try:
                status, _, _ = request(port, "GET", "/api/v1/bootstrap")
                if status == 200:
                    break
            except OSError:
                pass
            time.sleep(.1)
        else:
            raise AssertionError("GUI product startup timeout")
        # Browser processes and the extracted image must belong to this Home.
        helpers = list((home / "data/cache/webkit").glob("xs-window-*"))
        assert len(helpers) == 1, helpers
        time.sleep(2)
        import gi
        gi.require_version("Gtk", "3.0")
        gi.require_version("Gdk", "3.0")
        from gi.repository import Gdk, Gtk
        assert Gtk.init_check()[0]
        root = Gdk.get_default_root_window()
        picture = Gdk.pixbuf_get_from_window(root, 0, 0, root.get_width(), root.get_height())
        assert picture
        if args.screenshot:
            picture.savev(str(args.screenshot), "png", [], [])
        # Request shutdown through the native window's delete-event. X11 uses
        # WM_DELETE_WINDOW; select only the GTK window with this helper's PID.
        import ctypes
        x11 = ctypes.CDLL("libX11.so.6")
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        display = x11.XOpenDisplay(None)
        x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        x11.XInternAtom.restype = ctypes.c_ulong
        delete = x11.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
        protocols = x11.XInternAtom(display, b"WM_PROTOCOLS", 0)
        class EventData(ctypes.Union):
            _fields_ = [("b", ctypes.c_char * 20), ("s", ctypes.c_short * 10), ("l", ctypes.c_long * 5)]
        class ClientEvent(ctypes.Structure):
            _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                        ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), ("message_type", ctypes.c_ulong),
                        ("format", ctypes.c_int), ("data", EventData), ("pad", ctypes.c_long * 5)]
        x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]; x11.XDefaultRootWindow.restype = ctypes.c_ulong
        root_id = x11.XDefaultRootWindow(display)
        root_return = ctypes.c_ulong(); parent_return = ctypes.c_ulong(); children = ctypes.POINTER(ctypes.c_ulong)(); count = ctypes.c_uint()
        x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
        x11.XQueryTree(display, root_id, ctypes.byref(root_return), ctypes.byref(parent_return), ctypes.byref(children), ctypes.byref(count))
        x11.XFree.argtypes = [ctypes.c_void_p]
        pid_atom = x11.XInternAtom(display, b"_NET_WM_PID", 0)
        helper_pid = None
        for item in Path('/proc').iterdir():
            if not item.name.isdigit():
                continue
            try:
                if (item / 'cmdline').read_bytes().split(b'\0')[0] == str(helpers[0]).encode():
                    helper_pid = int(item.name); break
            except (OSError, PermissionError):
                continue
        assert helper_pid, 'no owned native helper'
        x11.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
            ctypes.c_long, ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
        window_id = None
        for index in range(count.value):
            type_return = ctypes.c_ulong(); format_return = ctypes.c_int()
            length = ctypes.c_ulong(); after = ctypes.c_ulong(); value = ctypes.POINTER(ctypes.c_ubyte)()
            x11.XGetWindowProperty(display, children[index], pid_atom, 0, 1, 0, 0,
                ctypes.byref(type_return), ctypes.byref(format_return), ctypes.byref(length), ctypes.byref(after), ctypes.byref(value))
            if value and length.value and format_return.value == 32:
                if ctypes.cast(value, ctypes.POINTER(ctypes.c_ulong))[0] == helper_pid:
                    window_id = children[index]
            if value: x11.XFree(value)
        x11.XFree(children)
        assert window_id, "no native application window"
        event = ClientEvent(); event.type = 33; event.display = display; event.window = window_id
        event.message_type = protocols; event.format = 32; event.data.l[0] = delete
        x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.c_void_p]
        x11.XSendEvent(display, window_id, 0, 0, ctypes.byref(event))
        x11.XFlush.argtypes = [ctypes.c_void_p]; x11.XFlush(display)
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]; x11.XCloseDisplay(display)
        process.wait(timeout=30)
        assert process.returncode == 0, log_path.read_text(errors="replace")
        result = {"passed": True, "binary": str(args.binary), "home": str(home),
                  "checks": ["packed native startup", "real frontend screenshot", "portable window cache", "native window close stops service"],
                  "log": str(log_path)}
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2)); return 0
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM); process.wait(timeout=30)
        log.close()


if __name__ == "__main__":
    raise SystemExit(main())
