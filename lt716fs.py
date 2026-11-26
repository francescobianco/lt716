"""MorseOS files over BLE (firmware/morseos/core/system/files.h), used by ./lt716 fs.

BandFS talks the protocol: the list of files (file 0), read, write. mount() shows the band as
a directory through FUSE, with a small ctypes binding to libfuse3 (no Python package needed):
files have size 0 and are read with direct I/O, like /proc; a write is sent when the file is
written (one write() call; several lines are sent as one value each), so `echo 99 > speed`
fails with "Invalid argument"; `: > file` (truncate, nothing written) sends an empty value on close.
"""

import ctypes
import ctypes.util
import errno
import os
import stat
import sys
import time

import dbus
from gi.repository import GLib

WIN = 16          # data bytes per notification
WCHUNK = 17       # data bytes per write packet
STATUS = {0: "ok", 1: "no such file", 2: "read only", 3: "bad value"}


class FsError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


class BandFS:
    def __init__(self, band, listen, cmd_uuid):
        self.b = band
        self.listen_fn = listen
        self.cmd_uuid = cmd_uuid
        self.rx = None
        listen(band, self._on)
        self.files = {}   # path -> (id, readable, writable)
        self.refresh()

    def _on(self, v):
        if len(v) >= 4 and v[3] == 0xFC and self.rx:
            self.rx(v)

    def _send(self, pkt):
        for attempt in range(2):
            try:
                self.b.char(self.cmd_uuid).WriteValue(dbus.Array(pkt, signature="y"),
                                                      {"type": "request"})
                return
            except dbus.DBusException as e:
                if attempt:
                    raise FsError(errno.EIO, f"band: {e.get_dbus_message()}")
                self.b.dump_notify = False          # reconnect, notifications are off again
                self.b.connect()
                self.listen_fn(self.b, lambda v: None)

    def _wait(self, secs, done):
        """run the GLib loop until done() or secs without any packet"""
        timer = [None]

        def fire():
            timer[0] = None
            self.b.loop.quit()
            return False

        def arm():
            if timer[0]:
                GLib.source_remove(timer[0])
            timer[0] = GLib.timeout_add(int(secs * 1000), fire)

        def rx(v):
            got(v)
            arm()
            if done():
                self.b.loop.quit()

        got = self.rx
        self.rx = rx
        arm()
        try:
            if not done():
                self.b.loop.run()
        finally:
            self.rx = got
            if timer[0]:
                GLib.source_remove(timer[0])

    def read_id(self, fid):
        data, end = {}, [None]

        def got(v):
            if v[2] != fid or v[:2] == b"\xff\xff":
                return
            off = int.from_bytes(v[:2], "little")
            data[off] = bytes(v[4:])
            if len(v) - 4 < WIN:
                end[0] = off + len(v) - 4

        def complete():
            return end[0] is not None and all(o in data for o in range(0, end[0], WIN))

        def next_off():  # first block still missing, from where the band streams again
            lim = end[0] if end[0] is not None else (max(data) + WIN if data else 0)
            o = next((o for o in range(0, lim, WIN) if o not in data), None)
            return o if o is not None or end[0] is not None else lim

        self.rx = got
        try:
            for attempt in range(5):
                if complete():
                    break
                self._send(bytes([0x40, fid]) + next_off().to_bytes(2, "little"))
                self._wait(2, complete)
            if not complete():
                raise FsError(errno.EIO, "band: read incomplete")
        finally:
            self.rx = None
        return b"".join(data[o] for o in sorted(data) if o < end[0])

    def write_id(self, fid, value):
        value = value[:39]          # the band keeps 39 bytes of a written line
        st = []
        self.rx = lambda v: st.append(v[4]) if v[:2] == b"\xff\xff" and v[2] == fid else None
        try:
            for off in range(0, len(value), WCHUNK):
                self._send(bytes([0x41, fid, off]) + value[off:off + WCHUNK])
            self._send(bytes([0x42, fid, len(value)]))
            self._wait(3, lambda: bool(st))
        finally:
            self.rx = None
        if not st:
            raise FsError(errno.EIO, "band: no answer to the write")
        if st[0]:
            code = {1: errno.ENOENT, 2: errno.EACCES, 3: errno.EINVAL}.get(st[0], errno.EIO)
            raise FsError(code, STATUS.get(st[0], f"error {st[0]}"))

    def refresh(self):
        self.files = {}
        for i, line in enumerate(self.read_id(0).decode(errors="replace").splitlines(), 1):
            if len(line) > 3:
                self.files[line[3:]] = (i, line[0] == "r", line[1] == "w")

    def lookup(self, path):
        if path not in self.files:
            raise FsError(errno.ENOENT, f"{path}: no such file")
        return self.files[path]

    def read(self, path):
        return self.read_id(self.lookup(path)[0])

    def write(self, path, value):
        self.write_id(self.lookup(path)[0], value)

    def dirs(self):
        d = {"/"}
        for p in self.files:
            parts = p.strip("/").split("/")
            for n in range(1, len(parts)):
                d.add("/" + "/".join(parts[:n]))
        return d


# ---------------------------------------------------------------- FUSE (libfuse3 3.x)

class Stat(ctypes.Structure):  # x86_64 struct stat
    _fields_ = [("st_dev", ctypes.c_uint64), ("st_ino", ctypes.c_uint64),
                ("st_nlink", ctypes.c_uint64), ("st_mode", ctypes.c_uint32),
                ("st_uid", ctypes.c_uint32), ("st_gid", ctypes.c_uint32),
                ("pad0", ctypes.c_int32), ("st_rdev", ctypes.c_uint64),
                ("st_size", ctypes.c_int64), ("st_blksize", ctypes.c_int64),
                ("st_blocks", ctypes.c_int64), ("st_atime", ctypes.c_int64),
                ("st_atime_ns", ctypes.c_int64), ("st_mtime", ctypes.c_int64),
                ("st_mtime_ns", ctypes.c_int64), ("st_ctime", ctypes.c_int64),
                ("st_ctime_ns", ctypes.c_int64), ("unused", ctypes.c_int64 * 3)]


class FuseArgs(ctypes.Structure):
    _fields_ = [("argc", ctypes.c_int), ("argv", ctypes.POINTER(ctypes.c_char_p)),
                ("allocated", ctypes.c_int)]


c_path = ctypes.c_char_p
c_off = ctypes.c_int64
P = ctypes.c_void_p
FILLER = ctypes.CFUNCTYPE(ctypes.c_int, P, ctypes.c_char_p, P, c_off, ctypes.c_int)
GETATTR = ctypes.CFUNCTYPE(ctypes.c_int, c_path, ctypes.POINTER(Stat), P)
TRUNCATE = ctypes.CFUNCTYPE(ctypes.c_int, c_path, c_off, P)
OPEN = ctypes.CFUNCTYPE(ctypes.c_int, c_path, P)
READ = ctypes.CFUNCTYPE(ctypes.c_int, c_path, P, ctypes.c_size_t, c_off, P)
WRITE = ctypes.CFUNCTYPE(ctypes.c_int, c_path, P, ctypes.c_size_t, c_off, P)
READDIR = ctypes.CFUNCTYPE(ctypes.c_int, c_path, P, FILLER, c_off, P, ctypes.c_int)
UTIMENS = ctypes.CFUNCTYPE(ctypes.c_int, c_path, P, P)

# struct fuse_operations of libfuse 3.x, in order
OPS = ["getattr", "readlink", "mknod", "mkdir", "unlink", "rmdir", "symlink", "rename",
       "link", "chmod", "chown", "truncate", "open", "read", "write", "statfs", "flush",
       "release", "fsync", "setxattr", "getxattr", "listxattr", "removexattr", "opendir",
       "readdir", "releasedir", "fsyncdir", "init", "destroy", "access", "create", "lock",
       "utimens", "bmap", "ioctl", "poll", "write_buf", "read_buf", "flock", "fallocate",
       "copy_file_range", "lseek"]
TYPES = {"getattr": GETATTR, "truncate": TRUNCATE, "open": OPEN, "read": READ,
         "write": WRITE, "flush": OPEN, "release": OPEN, "readdir": READDIR,
         "utimens": UTIMENS}


class FuseOps(ctypes.Structure):
    _fields_ = [(n, TYPES.get(n, P)) for n in OPS]


def mount(fs, mountpoint, log=print):
    lib = ctypes.CDLL(ctypes.util.find_library("fuse3") or "libfuse3.so.3")
    lib.fuse_new.restype = P
    lib.fuse_new.argtypes = [ctypes.POINTER(FuseArgs), ctypes.POINTER(FuseOps), ctypes.c_size_t, P]
    lib.fuse_mount.argtypes = [P, ctypes.c_char_p]
    lib.fuse_loop.argtypes = [P]
    lib.fuse_unmount.argtypes = [P]
    lib.fuse_destroy.argtypes = [P]
    lib.fuse_get_session.restype = P
    lib.fuse_get_session.argtypes = [P]
    lib.fuse_set_signal_handlers.argtypes = [P]
    lib.fuse_remove_signal_handlers.argtypes = [P]

    t0 = int(time.time())
    uid, gid = os.getuid(), os.getgid()
    cache = {}     # path -> content read at offset 0 (the rest of the read uses it)
    pending = {}   # path -> value so far (opened with O_TRUNC); None once something was sent

    def guard(fn):
        def wrapped(*a):
            try:
                return fn(*a)
            except FsError as e:
                if e.code != errno.ENOENT:  # file managers look for .Trash, .hidden...
                        log(f"{a[0].decode() if a and isinstance(a[0], bytes) else ''}: {e}")
                return -e.code
            except Exception as e:  # never let an exception cross into C
                log(f"error: {e!r}")
                return -errno.EIO
        return wrapped

    @guard
    def getattr_(path, st, fi):
        p = path.decode()
        ctypes.memset(st, 0, ctypes.sizeof(Stat))
        s = st.contents
        s.st_uid, s.st_gid = uid, gid
        s.st_atime = s.st_mtime = s.st_ctime = t0
        if p in fs.dirs():
            s.st_mode, s.st_nlink = stat.S_IFDIR | 0o755, 2
            return 0
        _, r, w = fs.lookup(p)
        s.st_mode = stat.S_IFREG | (0o444 if r else 0) | (0o200 if w else 0)
        s.st_nlink = 1
        return 0

    @guard
    def readdir_(path, buf, filler, off, fi, flags):
        p = path.decode().rstrip("/") + "/"
        names = {".", ".."}
        for f in list(fs.files) + list(fs.dirs()):
            if f.startswith(p) and f != p.rstrip("/") and len(f) > len(p):
                names.add(f[len(p):].split("/")[0])
        for n in sorted(names):
            filler(buf, n.encode(), None, 0, 0)
        return 0

    @guard
    def open_(path, fi):
        fs.lookup(path.decode())
        flags = ctypes.cast(fi, ctypes.POINTER(ctypes.c_uint32))
        flags[1] |= 2           # direct_io: size 0, read until the end, like /proc
        if flags[0] & os.O_TRUNC:
            pending[path.decode()] = b""
        return 0

    @guard
    def read_(path, buf, size, off, fi):
        p = path.decode()
        if off == 0 or p not in cache:
            cache[p] = fs.read(p)
        d = cache[p][off:off + size]
        ctypes.memmove(buf, d, len(d))
        return len(d)

    @guard
    def write_(path, buf, size, off, fi):
        p = path.decode()
        v = (pending.get(p) or b"")[:off] + ctypes.string_at(buf, size)
        pending[p] = None
        lines = [ln for ln in v.split(b"\n") if ln.strip()] or [v]
        for ln in lines:            # several lines: one value each (e.g. /clock/alarms)
            fs.write(p, ln)         # now: a bad value fails the write() of the shell
            log(f"{p} <- {ln.decode(errors='replace').strip()}")
        return size

    @guard
    def truncate_(path, length, fi):
        p = path.decode()
        fs.lookup(p)
        if pending.get(p, b"") is not None:
            pending[p] = b""
        return 0

    @guard
    def flush_(path, fi):
        return 0

    @guard
    def release_(path, fi):     # truncated and nothing written (": > file"): empty value
        p = path.decode()
        if pending.pop(p, None) == b"" and fs.lookup(p)[2]:
            fs.write(p, b"")
            log(f"{p} <- (empty)")
        return 0

    @guard
    def utimens_(path, ts, fi):  # touch
        return 0

    ops = FuseOps(getattr=GETATTR(getattr_), readdir=READDIR(readdir_), open=OPEN(open_),
                  read=READ(read_), write=WRITE(write_), truncate=TRUNCATE(truncate_),
                  flush=OPEN(flush_), release=OPEN(release_), utimens=UTIMENS(utimens_))
    argv = [b"lt716fs", b"-o", b"fsname=morseos,subtype=lt716"]
    args = FuseArgs(len(argv), (ctypes.c_char_p * len(argv))(*argv), 0)
    f = lib.fuse_new(ctypes.byref(args), ctypes.byref(ops), ctypes.sizeof(ops), None)
    if not f:
        raise FsError(errno.EIO, "fuse_new failed")
    if lib.fuse_mount(f, os.fsencode(os.path.abspath(mountpoint))) != 0:
        lib.fuse_destroy(f)
        raise FsError(errno.EIO, f"cannot mount on {mountpoint}")
    se = lib.fuse_get_session(f)
    lib.fuse_set_signal_handlers(se)
    log(f"band mounted on {mountpoint} ({len(fs.files)} files); Ctrl+C or "
        f"`fusermount3 -u {mountpoint}` to unmount")
    try:
        lib.fuse_loop(f)
    finally:
        lib.fuse_remove_signal_handlers(se)
        lib.fuse_unmount(f)
        lib.fuse_destroy(f)
        log("unmounted")
