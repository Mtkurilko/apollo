#!/usr/bin/env python3
"""Drive apollo inside a pty and print what it painted.

Apollo is a full screen program, so the only honest way to check it is to give
it a real terminal, send it keys, and look at the resulting screen. This runs
it under a pty of a fixed size, feeds a script of keystrokes, and renders the
final screen by replaying the escape sequences into a small grid model.

  scripts/drive.py [--cols N] [--rows N] [--wait S] key... -- [apollo args]

Keys are literal text, or names like <enter> <esc> <tab> <up> <c-a> <leader>.
"""
import codecs, os, pty, re, select, signal, sys, time, fcntl, termios, struct
import unicodedata


def char_width(ch):
    """Display columns for one character, the way a terminal counts them."""
    if unicodedata.combining(ch):
        return 0
    if unicodedata.east_asian_width(ch) in ("W", "F"):
        return 2
    return 1

KEYS = {
    "<enter>": "\r", "<esc>": "\x1b", "<tab>": "\t", "<space>": " ",
    "<up>": "\x1b[A", "<down>": "\x1b[B", "<right>": "\x1b[C", "<left>": "\x1b[D",
    "<home>": "\x1b[H", "<end>": "\x1b[F", "<pgup>": "\x1b[5~", "<pgdn>": "\x1b[6~",
    "<bs>": "\x7f", "<del>": "\x1b[3~", "<leader>": "\x00", "<f1>": "\x1bOP",
    "<s-tab>": "\x1b[Z", "<s-pgup>": "\x1b[5;2~",
}
for c in "abcdefghijklmnopqrstuvwxyz":
    KEYS[f"<c-{c}>"] = chr(ord(c) - 96)


class Grid:
    """Just enough of a terminal to see what Apollo drew."""

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.reset()

    def reset(self):
        self.cells = [[" "] * self.cols for _ in range(self.rows)]
        self.x = self.y = 0
        self.wrap = False
        self.pending = ""
        self.shape = "?"

    @staticmethod
    def incomplete(tail):
        """True when `tail` starts an escape sequence that has not finished."""
        if tail == "\x1b":
            return True
        if tail.startswith("\x1b["):
            return re.match(r"\x1b\[[0-9;?<>!$ ]*[A-Za-z@`]", tail) is None
        if tail.startswith("\x1b]"):
            return re.match(r"\x1b\][^\x07\x1b]*(\x07|\x1b\\)", tail) is None
        if tail.startswith("\x1b") and len(tail) < 3 and tail[1] in "()#*+":
            return True
        return False

    def put(self, ch):
        # Pending wrap, as a real terminal does it: filling the last column
        # does not move to the next line, the *next* character does. Without
        # this every full-width row is followed by a phantom blank one.
        width = char_width(ch)
        if width == 0:
            return
        if self.wrap:
            self.x = 0
            self.y = min(self.y + 1, self.rows - 1)
            self.wrap = False
        if self.x + width > self.cols:
            self.x = 0
            self.y = min(self.y + 1, self.rows - 1)
        if self.y < self.rows and self.x < self.cols:
            self.cells[self.y][self.x] = ch
            # A wide glyph owns the column to its right; marking it empty keeps
            # the row's printed length equal to its column count.
            if width == 2 and self.x + 1 < self.cols:
                self.cells[self.y][self.x + 1] = ""
        if self.x + width >= self.cols:
            self.x = self.cols - 1
            self.wrap = True
        else:
            self.x += width

    def feed(self, data):
        # A read can end in the middle of an escape sequence. Keep the tail and
        # prepend it next time, or the sequence gets printed as text.
        data = self.pending + data
        self.pending = ""
        i = 0
        while i < len(data):
            c = data[i]
            if c == "\x1b":
                tail = data[i:]
                if self.incomplete(tail):
                    self.pending = tail
                    return
                m = re.match(r"\x1b\[([0-9;?<>!$ ]*)([A-Za-z@`])", tail)
                if m:
                    self.csi(m.group(1), m.group(2))
                    i += m.end()
                    continue
                m = re.match(r"\x1b\][^\x07\x1b]*(\x07|\x1b\\)", tail)
                if m:
                    i += m.end()
                    continue
                m = re.match(r"\x1b[()#*+][0-9A-Za-z]", tail)
                if m:
                    i += m.end()
                    continue
                i += 2
                continue
            if c == "\n":
                self.y = min(self.y + 1, self.rows - 1)
                self.wrap = False
            elif c == "\r":
                self.x = 0
                self.wrap = False
            elif c == "\b":
                self.x = max(0, self.x - 1)
            elif c == "\t":
                self.x = min(self.cols - 1, (self.x // 8 + 1) * 8)
            elif c >= " ":
                self.put(c)
            i += 1

    def csi(self, params, final):
        if final == "q":
            self.shape = params
            return
        if final in "HABCDGJK":
            self.wrap = False
        nums = [int(p) for p in params.replace("?", "").split(";") if p.isdigit()]
        n = nums[0] if nums else 1
        if final == "H":
            self.y = (nums[0] - 1) if len(nums) > 0 else 0
            self.x = (nums[1] - 1) if len(nums) > 1 else 0
            self.y = max(0, min(self.y, self.rows - 1))
            self.x = max(0, min(self.x, self.cols - 1))
        elif final == "A": self.y = max(0, self.y - n)
        elif final == "B": self.y = min(self.rows - 1, self.y + n)
        elif final == "C": self.x = min(self.cols - 1, self.x + n)
        elif final == "D": self.x = max(0, self.x - n)
        elif final == "G": self.x = max(0, min((nums[0] if nums else 1) - 1, self.cols - 1))
        elif final == "J":
            mode = nums[0] if nums else 0
            if mode == 2:
                self.cells = [[" "] * self.cols for _ in range(self.rows)]
            elif mode == 0:
                for x in range(self.x, self.cols): self.cells[self.y][x] = " "
                for y in range(self.y + 1, self.rows): self.cells[y] = [" "] * self.cols
        elif final == "K":
            mode = nums[0] if nums else 0
            if mode == 0:
                for x in range(self.x, self.cols): self.cells[self.y][x] = " "
            elif mode == 2:
                self.cells[self.y] = [" "] * self.cols

    def text(self):
        return "\n".join("".join(r).rstrip() for r in self.cells)


def main():
    cols, rows, settle = 100, 30, 0.6
    args = sys.argv[1:]
    while args and args[0].startswith("--") and args[0] != "--":
        flag = args.pop(0)
        if flag == "--cols": cols = int(args.pop(0))
        elif flag == "--rows": rows = int(args.pop(0))
        elif flag == "--wait": settle = float(args.pop(0))
        else: sys.exit("unknown flag: " + flag)

    keys, apollo_args = [], []
    if "--" in args:
        cut = args.index("--")
        keys, apollo_args = args[:cut], args[cut + 1:]
    else:
        keys = args

    pid, fd = pty.fork()
    if pid == 0:
        # Set the window size on the slave before exec: doing it from the
        # parent races with the child reading its own size at startup.
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        os.environ["COLUMNS"] = str(cols)
        os.environ["LINES"] = str(rows)
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ["LANG"] = "en_US.UTF-8"
        try:
            os.execv("./build/apollo", ["apollo"] + apollo_args)
        finally:
            os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    grid = Grid(rows, cols)
    # A read can also split a UTF-8 character in half; an incremental decoder
    # holds the leftover bytes instead of turning them into U+FFFD.
    decoder = codecs.getincrementaldecoder("utf-8")("replace")

    def drain(seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], max(0.0, end - time.time()))
            if not r:
                continue
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                return False
            if not chunk:
                return False
            grid.feed(decoder.decode(chunk))
        return True

    drain(settle)
    for key in keys:
        # <sh:...> runs a shell command mid-session, for checking things that
        # happen outside Apollo — a config file edited in another window, say.
        if key.startswith("<sh:") and key.endswith(">"):
            os.system(key[4:-1])
            drain(1.2)
            continue
        # Mouse, in SGR encoding, 1-based like the protocol itself:
        #   <click:X,Y>  <dblclick:X,Y>  <drag:X1,Y1,X2,Y2>  <wheel:X,Y,up|down>
        if key.startswith("<click:") or key.startswith("<dblclick:"):
            x, y = (int(v) for v in key[key.index(":") + 1:-1].split(","))
            times = 2 if key.startswith("<dbl") else 1
            for _ in range(times):
                os.write(fd, f"\x1b[<0;{x};{y}M".encode())
                drain(0.05)
                os.write(fd, f"\x1b[<0;{x};{y}m".encode())
                drain(0.05)
            drain(0.5)
            continue
        if key.startswith("<drag:"):
            x1, y1, x2, y2 = (int(v) for v in key[6:-1].split(","))
            os.write(fd, f"\x1b[<0;{x1};{y1}M".encode())
            drain(0.05)
            os.write(fd, f"\x1b[<32;{x2};{y2}M".encode())
            drain(0.05)
            os.write(fd, f"\x1b[<0;{x2};{y2}m".encode())
            drain(0.5)
            continue
        if key.startswith("<wheel:"):
            x, y, direction = key[7:-1].split(",")
            code = 64 if direction == "up" else 65
            os.write(fd, f"\x1b[<{code};{x};{y}M".encode())
            drain(0.35)
            continue
        # <size:COLSxROWS> resizes the window, which is the one thing a full
        # screen program has to get right and the easiest thing to get wrong.
        if key.startswith("<size:") and key.endswith(">"):
            w, h = key[6:-1].split("x")
            grid.rows, grid.cols = int(h), int(w)
            grid.reset()
            fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", int(h), int(w), 0, 0))
            os.kill(pid, signal.SIGWINCH)
            drain(1.0)
            continue
        os.write(fd, KEYS.get(key, key).encode())
        drain(0.35)

    print(grid.text())
    if os.environ.get("DRIVE_CURSOR"):
        print(f"[cursor at row {grid.y + 1}, col {grid.x + 1}; shape {grid.shape}]",
              file=sys.stderr)

    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    _, status = os.waitpid(pid, 0)
    if os.WIFEXITED(status):
        print(f"\n[apollo exited {os.WEXITSTATUS(status)}]", file=sys.stderr)
    os.close(fd)


main()
