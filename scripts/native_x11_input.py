"""Scoped X11 keyboard/pointer input for the disposable native browser only."""
import ctypes
import ctypes.util
import re
import subprocess


class NativeX11Input:
    def __init__(self, pid, title, parent_title=None):
        # Views bubbles can share their parent's X11 host. Scope input to a
        # verified client of this fixture PID, never an unrelated window.
        clients = subprocess.run(["xprop", "-root", "_NET_CLIENT_LIST"], check=True,
                                 capture_output=True, text=True, timeout=5).stdout
        owned = []
        for window in re.findall(r"0x[0-9a-f]+", clients):
            owner = subprocess.run(["xprop", "-id", window, "_NET_WM_PID", "_NET_WM_NAME"],
                                   check=True, capture_output=True, text=True, timeout=5).stdout
            if re.search(r"_NET_WM_PID.*= " + str(pid) + r"\b", owner):
                owned.append((window, owner))
        matches = [item for item in owned if (parent_title or title) in item[1]]
        if len(matches) == 1:
            self.window = int(matches[0][0], 16)
        elif len(owned) == 1 and not parent_title:
            self.window = int(owned[0][0], 16)
        else:
            raise RuntimeError("Cannot identify one X11 client belonging to the test dialog")
        x11_path, xtest_path = ctypes.util.find_library("X11"), ctypes.util.find_library("Xtst")
        if not x11_path or not xtest_path:
            raise RuntimeError("Native X11 keyboard smoke requires existing libX11 and libXtst")
        self.x11 = ctypes.CDLL(x11_path)
        self.xtest = ctypes.CDLL(xtest_path)
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_uint
        self.x11.XkbKeycodeToKeysym.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_int]
        self.x11.XkbKeycodeToKeysym.restype = ctypes.c_ulong
        self.x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        self.x11.XGetInputFocus.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int)]
        self.x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self.xtest.XTestFakeKeyEvent.restype = ctypes.c_int

    def replace(self, text):
        if not text.isascii():
            raise ValueError("This test driver supports ASCII fixture input only")
        display = self.x11.XOpenDisplay(None)
        if not display:
            raise RuntimeError("No X11 test display")
        control = shift = None
        try:
            self.x11.XSetInputFocus(display, self.window, 2, 0)
            self.x11.XSync(display, 0)
            focus, revert = ctypes.c_ulong(), ctypes.c_int()
            self.x11.XGetInputFocus(display, ctypes.byref(focus), ctypes.byref(revert))
            if focus.value != self.window:
                raise RuntimeError("Refusing keyboard input without test-dialog focus")
            def key(symbol, down):
                code = self.x11.XKeysymToKeycode(display, symbol)
                if not code or not self.xtest.XTestFakeKeyEvent(display, code, down, 0):
                    raise RuntimeError("Cannot synthesize fixture key")
            control, shift = 0xffe3, 0xffe1
            key(control, 1)
            key(ord('a'), 1)
            key(ord('a'), 0)
            key(control, 0)
            for char in text:
                symbol = ord(char)
                code = self.x11.XKeysymToKeycode(display, symbol)
                shifted = self.x11.XkbKeycodeToKeysym(display, code, 0, 0) != symbol
                if shifted and self.x11.XkbKeycodeToKeysym(display, code, 0, 1) != symbol:
                    raise RuntimeError("Fixture requires an ASCII-compatible X11 keyboard layout")
                if shifted:
                    key(shift, 1)
                key(symbol, 1)
                key(symbol, 0)
                if shifted:
                    key(shift, 0)
            self.x11.XSync(display, 0)
        finally:
            # Never leave a modifier held if an input assertion fails.
            for modifier in (control, shift):
                if modifier:
                    code = self.x11.XKeysymToKeycode(display, modifier)
                    self.xtest.XTestFakeKeyEvent(display, code, 0, 0)
            self.x11.XSync(display, 0)
            self.x11.XCloseDisplay(display)

    def press_key(self, symbol, control=False, alt=False, shift=False):
        """Send one key/chord only after focusing the verified fixture client."""
        display = self.x11.XOpenDisplay(None)
        if not display:
            raise RuntimeError("No X11 test display")
        control_code = self.x11.XKeysymToKeycode(display, 0xffe3)
        alt_code = self.x11.XKeysymToKeycode(display, 0xffe9)
        shift_code = self.x11.XKeysymToKeycode(display, 0xffe1)
        try:
            self.x11.XSetInputFocus(display, self.window, 2, 0)
            self.x11.XSync(display, 0)
            focus, revert = ctypes.c_ulong(), ctypes.c_int()
            self.x11.XGetInputFocus(display, ctypes.byref(focus), ctypes.byref(revert))
            if focus.value != self.window:
                raise RuntimeError("Refusing keyboard input without test-client focus")
            code = self.x11.XKeysymToKeycode(display, symbol)
            if not code or (control and not control_code) or (alt and not alt_code) or (shift and not shift_code):
                raise RuntimeError("No fixture key in the X11 keyboard layout")
            if control:
                self.xtest.XTestFakeKeyEvent(display, control_code, 1, 0)
            if alt:
                self.xtest.XTestFakeKeyEvent(display, alt_code, 1, 0)
            if shift:
                self.xtest.XTestFakeKeyEvent(display, shift_code, 1, 0)
            self.xtest.XTestFakeKeyEvent(display, code, 1, 0)
            self.xtest.XTestFakeKeyEvent(display, code, 0, 0)
            self.x11.XSync(display, 0)
        finally:
            if shift and shift_code:
                self.xtest.XTestFakeKeyEvent(display, shift_code, 0, 0)
            if alt and alt_code:
                self.xtest.XTestFakeKeyEvent(display, alt_code, 0, 0)
            if control and control_code:
                self.xtest.XTestFakeKeyEvent(display, control_code, 0, 0)
            self.x11.XSync(display, 0)
            self.x11.XCloseDisplay(display)

    def click(self, x, y):
        """Click only inside this verified fixture window."""
        self._pointer(x, y, click=True)

    def move(self, x, y):
        """Move without clicking, only inside this verified fixture window."""
        self._pointer(x, y, click=False)

    def _pointer(self, x, y, click):
        """Use client-relative coordinates, validating bounds before any input."""
        if x < 0 or y < 0:
            raise ValueError("Fixture click must be inside its client window")
        x11, xtest = self.x11, self.xtest
        x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x11.XDefaultRootWindow.restype = ctypes.c_ulong
        x11.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                    ctypes.POINTER(ctypes.c_ulong)] + [ctypes.POINTER(ctypes.c_int)] * 2 + [ctypes.POINTER(ctypes.c_uint)] * 4
        x11.XTranslateCoordinates.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                              ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int),
                                              ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong)]
        xtest.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
        xtest.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        display = x11.XOpenDisplay(None)
        if not display:
            raise RuntimeError("No X11 test display")
        try:
            root, child = ctypes.c_ulong(), ctypes.c_ulong()
            gx, gy, sx, sy = (ctypes.c_int() for _ in range(4))
            width, height, border, depth = (ctypes.c_uint() for _ in range(4))
            if not x11.XGetGeometry(display, self.window, ctypes.byref(root), ctypes.byref(gx), ctypes.byref(gy),
                                    ctypes.byref(width), ctypes.byref(height), ctypes.byref(border), ctypes.byref(depth)):
                raise RuntimeError("Cannot locate fixture window")
            if x >= width.value or y >= height.value:
                raise ValueError("Fixture click must be inside its client window")
            if not x11.XTranslateCoordinates(display, self.window, x11.XDefaultRootWindow(display), x, y,
                                              ctypes.byref(sx), ctypes.byref(sy), ctypes.byref(child)):
                raise RuntimeError("Cannot translate fixture click")
            xtest.XTestFakeMotionEvent(display, -1, sx.value, sy.value, 0)
            if click:
                xtest.XTestFakeButtonEvent(display, 1, 1, 0)
                xtest.XTestFakeButtonEvent(display, 1, 0, 0)
            x11.XSync(display, 0)
        finally:
            x11.XCloseDisplay(display)
