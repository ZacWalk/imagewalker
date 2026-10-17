#!/usr/bin/env python3
"""Compile an HTML Help project (.hhp) into a .chm.

Replaces hhc.exe from HTML Help Workshop, which Microsoft withdrew and which
only ever existed as a 32-bit binary. The output is a standard ITSF container
that Windows' own hh.exe reads: it opens, it decompiles, and HH_HELP_CONTEXT
resolves through the #IVB map exactly as it did in 2001.

The only deliberate departure from hhc.exe is that content is stored
uncompressed (ITSF section 0). LZX would save perhaps 60% of a 250KB file and
costs an encoder; the container, the directory, and every lookup table are the
real format.

Format references: the ITSF/ITSP layout is described in Matthew Russotto's
"Microsoft's HTML Help (.chm) format" notes and in the chmspec document; the
internal-file layouts (#SYSTEM, #IVB, #STRINGS, #WINDOWS) come from the same.

Usage:
    python chmbuild.py <project.hhp> [-o output.chm] [--verbose]
"""

from __future__ import annotations

import argparse
import html
import re
import struct
import sys
import time
from pathlib import Path

# --- container constants ----------------------------------------------------

CHUNK_SIZE = 0x1000
# "quickref density": the directory carries a lookup shortcut to every
# QUICKREF_STRIDEth entry within a chunk. 2 is what every real CHM uses.
DENSITY = 2
QUICKREF_STRIDE = (1 << DENSITY) + 1

GUID_ITSF_A = bytes.fromhex("10fd017caa7bd0119e0c00a0c922e6ec")
GUID_ITSF_B = bytes.fromhex("11fd017caa7bd0119e0c00a0c922e6ec")
GUID_ITSP = bytes.fromhex("6a92025d2e21d0119df900a0c922e6ec")

# #SYSTEM entry codes we emit.
SYS_CONTENTS_FILE = 0
SYS_INDEX_FILE = 1
SYS_DEFAULT_TOPIC = 2
SYS_TITLE = 3
SYS_FLAGS = 4
SYS_DEFAULT_WINDOW = 5
SYS_COMPILED_FILE = 6
SYS_BINARY_INDEX = 7
SYS_COMPILER = 9
SYS_TIMESTAMP = 10


class BuildError(Exception):
    pass


# --- variable-length integers ----------------------------------------------


def encint(value: int) -> bytes:
    """ENCINT: base-128, most significant group first, continuation bit high."""
    if value < 0:
        raise ValueError("ENCINT cannot encode a negative value")
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


# --- .hhp project -----------------------------------------------------------


class Project:
    """A parsed .hhp, resolved against the directory that contains it."""

    def __init__(self, path: Path):
        self.path = path
        self.root = path.parent
        self.options: dict[str, str] = {}
        self.files: list[str] = []
        self.alias: dict[str, str] = {}
        self.windows: list[str] = []
        self.context: dict[int, str] = {}
        self.warnings: list[str] = []
        self._parse()

    # -- parsing

    def _parse(self) -> None:
        section = None
        defines: dict[str, int] = {}
        alias_raw: dict[str, str] = {}

        for raw in self.path.read_text(encoding="mbcs", errors="replace").splitlines():
            line = raw.strip()
            if not line or line.startswith(";"):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1].upper()
                continue

            if section == "OPTIONS":
                key, _, value = line.partition("=")
                self.options[key.strip().lower()] = value.strip()
            elif section == "FILES":
                self.files.append(line)
            elif section == "WINDOWS":
                self.windows.append(line)
            elif section == "ALIAS":
                key, _, value = line.partition("=")
                key = key.strip()
                # HHW tolerated stray option-looking lines here; skip them.
                if key.lower() in ("compatibility", "compiled file"):
                    continue
                if value.strip():
                    alias_raw[key.upper()] = value.strip()
            elif section == "MAP":
                if line.lower().startswith("#include"):
                    defines.update(self._read_map_include(line))
                else:
                    m = re.match(r"#define\s+(\w+)\s+(\S+)", line)
                    if m:
                        defines[m.group(1).upper()] = _parse_int(m.group(2))

        self.alias = alias_raw
        for name, number in defines.items():
            target = alias_raw.get(name)
            if target is None:
                self.warnings.append(f"[MAP] {name} = {number} has no [ALIAS] entry")
                continue
            self.context[number] = target
        for name in alias_raw:
            if name not in defines:
                self.warnings.append(f"[ALIAS] {name} has no [MAP] #define")

    def _read_map_include(self, line: str) -> dict[str, int]:
        spec = line.split(None, 1)[1].strip().strip('"<>')
        target = (self.root / spec.replace("\\", "/")).resolve()
        if not target.is_file():
            raise BuildError(f"[MAP] #include not found: {spec} (looked in {target})")
        defines: dict[str, int] = {}
        for raw in target.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"\s*#define\s+(\w+)\s+(\S+)\s*$", raw)
            if m and not m.group(2).startswith("_"):
                try:
                    defines[m.group(1).upper()] = _parse_int(m.group(2))
                except ValueError:
                    continue
        return defines

    # -- options

    def option(self, key: str, default: str = "") -> str:
        return self.options.get(key.lower(), default)

    @property
    def title(self) -> str:
        return self.option("title") or self.option("default topic") or "Help"

    @property
    def language(self) -> int:
        # "0x809 English (United Kingdom)" -> 0x809
        raw = self.option("language", "0x409").split()[0]
        return _parse_int(raw)

    @property
    def output(self) -> Path:
        spec = self.option("compiled file") or (self.path.stem + ".chm")
        return (self.root / spec.replace("\\", "/")).resolve()


def _parse_int(text: str) -> int:
    text = text.strip().rstrip(",")
    return int(text, 16) if text.lower().startswith("0x") else int(text, 10)


# --- gathering the content --------------------------------------------------

# href="x", src='x', and the sitemap <param name="Local" value="x">.
_REF = re.compile(
    r"""(?:href|src)\s*=\s*["']([^"'>]+)["']"""
    r"""|name\s*=\s*["']Local["']\s+value\s*=\s*["']([^"'>]+)["']""",
    re.IGNORECASE,
)


def gather(project: Project, verbose: bool = False) -> list[Path]:
    """Every file that belongs in the CHM.

    Seeded from [FILES], the contents/index files and the default topic, then
    closed over the links those files contain. hhc.exe did the same implicit
    pull-in, which is why the historical [FILES] list could drift out of date
    without anyone noticing.
    """
    seeds = list(project.files)
    for key in ("contents file", "index file", "default topic"):
        value = project.option(key)
        if value:
            seeds.append(value)

    found: dict[str, Path] = {}
    queue: list[tuple[str, str]] = [(s, "[FILES]") for s in seeds]
    missing: list[tuple[str, str]] = []

    while queue:
        spec, origin = queue.pop(0)
        rel = _normalise(spec)
        if rel is None:
            continue
        key = rel.lower()
        if key in found:
            continue
        target = project.root / rel
        if not target.is_file():
            resolved = _find_case_insensitive(project.root, rel)
            if resolved is None:
                missing.append((rel, origin))
                continue
            target = resolved
            rel = target.relative_to(project.root).as_posix()
            key = rel.lower()
            if key in found:
                continue
        found[key] = target

        if target.suffix.lower() in (".html", ".htm", ".hhc", ".hhk"):
            text = target.read_text(encoding="mbcs", errors="replace")
            for m in _REF.finditer(text):
                ref = m.group(1) or m.group(2)
                queue.append((html.unescape(ref), rel))

    for rel, origin in missing:
        project.warnings.append(f"missing file: {rel} (referenced by {origin})")

    listed = {_normalise(f).lower() for f in project.files if _normalise(f)}
    for key in sorted(found):
        if key not in listed and verbose:
            print(f"  pulled in: {found[key].name}")

    return [found[k] for k in sorted(found)]


def _normalise(spec: str) -> str | None:
    """A project-relative POSIX path, or None if this is not a local file."""
    spec = spec.strip().split("#", 1)[0].strip()
    if not spec:
        return None
    if re.match(r"^[a-z][a-z0-9+.-]*:", spec, re.IGNORECASE):
        return None  # http:, mailto:, javascript:, ms-its: ...
    return spec.replace("\\", "/").lstrip("/")


def _find_case_insensitive(root: Path, rel: str) -> Path | None:
    current = root
    for part in rel.split("/"):
        if not current.is_dir():
            return None
        match = next((c for c in current.iterdir() if c.name.lower() == part.lower()), None)
        if match is None:
            return None
        current = match
    return current if current.is_file() else None


# --- internal files ---------------------------------------------------------


class StringPool:
    """#STRINGS: NUL-terminated strings, none allowed to straddle a 4K block."""

    def __init__(self) -> None:
        self.data = bytearray(b"\x00")
        self.offsets: dict[str, int] = {}

    def add(self, text: str) -> int:
        if text in self.offsets:
            return self.offsets[text]
        blob = text.encode("mbcs", errors="replace") + b"\x00"
        if len(self.data) % CHUNK_SIZE + len(blob) > CHUNK_SIZE:
            self.data.extend(b"\x00" * (CHUNK_SIZE - len(self.data) % CHUNK_SIZE))
        offset = len(self.data)
        self.data.extend(blob)
        self.offsets[text] = offset
        return offset


def build_namelist(sections: list[str]) -> bytes:
    body = bytearray()
    for name in sections:
        body += struct.pack("<H", len(name))
        body += name.encode("utf-16-le")
        body += b"\x00\x00"
    out = struct.pack("<HH", (len(body) + 4) // 2, len(sections)) + bytes(body)
    return out


def build_system(project: Project, strings_lcid: int) -> bytes:
    entries: list[tuple[int, bytes]] = []

    def add_str(code: int, value: str) -> None:
        entries.append((code, value.encode("mbcs", errors="replace") + b"\x00"))

    contents = project.option("contents file")
    index = project.option("index file")
    default_topic = project.option("default topic")

    if contents:
        add_str(SYS_CONTENTS_FILE, contents)
    if index:
        add_str(SYS_INDEX_FILE, index)
    if default_topic:
        add_str(SYS_DEFAULT_TOPIC, default_topic)
    add_str(SYS_TITLE, project.title)

    # code 4: LCID, DBCS, full-text-search, has-KLinks, has-ALinks, then padding
    entries.append(
        (
            SYS_FLAGS,
            struct.pack("<9I", strings_lcid, 0, 0, 0, 0, 0, 0, 0, 0),
        )
    )

    if project.windows:
        window = project.option("default window")
        if window:
            add_str(SYS_DEFAULT_WINDOW, window)
    add_str(SYS_COMPILED_FILE, project.output.name)
    entries.append((SYS_BINARY_INDEX, struct.pack("<I", 0)))
    add_str(SYS_COMPILER, "chmbuild 1.0")
    entries.append((SYS_TIMESTAMP, struct.pack("<I", int(time.time()) & 0xFFFFFFFF)))

    out = bytearray(struct.pack("<I", 3))
    for code, blob in entries:
        out += struct.pack("<HH", code, len(blob)) + blob
    return bytes(out)


def build_ivb(context: dict[int, str], pool: StringPool) -> bytes:
    body = bytearray()
    for cid in sorted(context):
        target = _normalise(context[cid])
        if target is None:
            continue
        # Root-relative with no leading slash: that is the form the viewer
        # hands back to the topic resolver.
        body += struct.pack("<II", cid, pool.add(target.lower()))
    return struct.pack("<I", len(body)) + bytes(body)


def build_windows(project: Project, pool: StringPool) -> bytes:
    """#WINDOWS: one HH_WINTYPE per [WINDOWS] line, with string pointers
    replaced by #STRINGS offsets and the HWND fields left zero.

    The .hhp field order is the one HTML Help Workshop documented:
    0 title, 1 toc, 2 index, 3 default topic, 4 home, 5-8 jump buttons,
    9 window properties, 10 nav pane width, 11 toolbar buttons,
    12 [left,top,right,bottom], 13 style, 14 exstyle, 15 show state,
    16 nav pane closed, 17 default tab, 18 tab position, 19 -.
    """
    if not project.windows:
        return b""

    entries = bytearray()
    for line in project.windows:
        name, _, rest = line.partition("=")
        fields = _split_window_fields(rest)

        def field(i: int, default: str = "") -> str:
            value = fields[i] if i < len(fields) else ""
            return value or default

        rect = _parse_rect(field(12)) or (0, 0, 0, 0)
        properties = _parse_int(field(9, "0x62120"))
        toolbar = _parse_int(field(11, "0x3006"))
        nav_width = int(field(10, "200"))
        show_state = int(field(15, "5"))  # SW_SHOW
        nav_closed = int(field(16, "0"))
        default_tab = int(field(17, "0"))

        entry = bytearray(0xC4)
        struct.pack_into("<I", entry, 0x00, 0xC4)  # cbStruct
        struct.pack_into("<I", entry, 0x04, 0)  # fUniCodeStrings
        struct.pack_into("<I", entry, 0x08, pool.add(name.strip()))  # pszType
        # fsValidMembers: properties, rect, nav width, show state, toolbar flags,
        # expansion, current tab. Anything not flagged here is ignored, which is
        # why a window with a good rect but no HHWIN_PARAM_RECT opens invisible.
        struct.pack_into("<I", entry, 0x0C, 0x02 | 0x10 | 0x20 | 0x40 | 0x100 | 0x200 | 0x2000)
        struct.pack_into("<I", entry, 0x10, properties)  # fsWinProperties
        struct.pack_into("<I", entry, 0x14, pool.add(field(0, project.title)))  # pszCaption
        struct.pack_into("<I", entry, 0x18, _parse_int(field(13, "0")))  # dwStyles
        struct.pack_into("<I", entry, 0x1C, _parse_int(field(14, "0")))  # dwExStyles
        struct.pack_into("<4i", entry, 0x20, *rect)  # rcWindowPos
        struct.pack_into("<i", entry, 0x30, show_state)  # nShowState
        struct.pack_into("<i", entry, 0x4C, nav_width)  # iNavWidth
        struct.pack_into("<I", entry, 0x60, _opt_str(pool, field(1) or project.option("contents file")))
        struct.pack_into("<I", entry, 0x64, _opt_str(pool, field(2) or project.option("index file")))
        struct.pack_into("<I", entry, 0x68, _opt_str(pool, field(3) or project.option("default topic")))
        struct.pack_into("<I", entry, 0x6C, _opt_str(pool, field(4) or project.option("default topic")))
        struct.pack_into("<I", entry, 0x70, toolbar)  # fsToolBarFlags
        struct.pack_into("<i", entry, 0x74, nav_closed)  # fNotExpanded
        struct.pack_into("<i", entry, 0x78, default_tab)  # curNavType
        entries += entry

    return struct.pack("<II", len(project.windows), 0xC4) + bytes(entries)


def _opt_str(pool: StringPool, value: str) -> int:
    return pool.add(value) if value else 0


def _parse_rect(text: str) -> tuple[int, int, int, int] | None:
    m = re.match(r"\[\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\]", text.strip())
    return tuple(int(g) for g in m.groups()) if m else None  # type: ignore[return-value]


def _split_window_fields(rest: str) -> list[str]:
    """Split a [WINDOWS] definition on commas, respecting quotes and [a,b,c,d]."""
    fields: list[str] = []
    current = ""
    quoted = False
    depth = 0
    for ch in rest:
        if ch == '"':
            quoted = not quoted
            continue
        if ch == "[" and not quoted:
            depth += 1
        elif ch == "]" and not quoted:
            depth -= 1
        if ch == "," and not quoted and depth == 0:
            fields.append(current.strip())
            current = ""
        else:
            current += ch
    fields.append(current.strip())
    return fields


# --- the container ----------------------------------------------------------


class Entry:
    __slots__ = ("name", "data", "offset")

    def __init__(self, name: str, data: bytes):
        self.name = name
        self.data = data
        self.offset = 0


def build_directory(entries: list[Entry], lcid: int) -> bytes:
    """ITSP header plus one or more 4K PMGL listing chunks."""
    chunks: list[bytearray] = []
    current = bytearray()
    counts: list[int] = []
    count = 0

    def quickref_size(n: int) -> int:
        # trailing entry count, plus one WORD per shortcut
        return 2 + 2 * (max(n - 1, 0) // QUICKREF_STRIDE)

    def flush() -> None:
        nonlocal current, count
        chunks.append(current)
        counts.append(count)
        current = bytearray()
        count = 0

    for entry in entries:
        name = entry.name.encode("utf-8")
        blob = (
            encint(len(name))
            + name
            + encint(0)
            + encint(entry.offset)
            + encint(len(entry.data))
        )
        header_and_body = 0x14 + len(current) + len(blob)
        if count and header_and_body + quickref_size(count + 1) > CHUNK_SIZE:
            flush()
        current += blob
        count += 1
    if count or not chunks:
        flush()

    out = bytearray()
    for i, body in enumerate(chunks):
        n = counts[i]
        chunk = bytearray(CHUNK_SIZE)
        chunk[0:4] = b"PMGL"
        # This field is "free space AND quickref area", i.e. everything after the
        # last entry. Subtracting the quickref as well leaves the reader parsing
        # past the end of the entries, and it rejects the whole file.
        free = CHUNK_SIZE - 0x14 - len(body)
        struct.pack_into("<I", chunk, 0x04, free)
        struct.pack_into("<I", chunk, 0x08, 0)
        struct.pack_into("<i", chunk, 0x0C, i - 1 if i else -1)
        struct.pack_into("<i", chunk, 0x10, i + 1 if i + 1 < len(chunks) else -1)
        chunk[0x14 : 0x14 + len(body)] = body

        # Quickref: entry count last, then shortcut offsets growing backwards.
        # The offsets are measured from the first entry, not from the chunk.
        struct.pack_into("<H", chunk, CHUNK_SIZE - 2, n)
        offsets = _entry_offsets(body)
        for j in range(1, (max(n - 1, 0) // QUICKREF_STRIDE) + 1):
            index = j * QUICKREF_STRIDE
            struct.pack_into("<H", chunk, CHUNK_SIZE - 2 - 2 * j, offsets[index])
        out += chunk

    header = bytearray(0x54)
    header[0:4] = b"ITSP"
    struct.pack_into("<I", header, 0x04, 1)
    struct.pack_into("<I", header, 0x08, 0x54)
    struct.pack_into("<I", header, 0x0C, 0x0A)
    struct.pack_into("<I", header, 0x10, CHUNK_SIZE)
    struct.pack_into("<I", header, 0x14, DENSITY)
    struct.pack_into("<I", header, 0x18, 1)  # depth: listing chunks only
    struct.pack_into("<i", header, 0x1C, -1)  # no PMGI index chunk
    struct.pack_into("<I", header, 0x20, 0)
    struct.pack_into("<I", header, 0x24, len(chunks) - 1)
    struct.pack_into("<i", header, 0x28, -1)
    struct.pack_into("<I", header, 0x2C, len(chunks))
    struct.pack_into("<I", header, 0x30, lcid)
    header[0x34:0x44] = GUID_ITSP
    struct.pack_into("<I", header, 0x44, 0x54)
    struct.pack_into("<i", header, 0x48, -1)
    struct.pack_into("<i", header, 0x4C, -1)
    struct.pack_into("<i", header, 0x50, -1)

    return bytes(header) + bytes(out)


def _entry_offsets(body: bytes) -> list[int]:
    """Byte offset of each entry within a chunk body."""
    offsets = []
    pos = 0
    while pos < len(body):
        offsets.append(pos)
        length, pos = _decint(body, pos)
        pos += length
        for _ in range(3):
            _, pos = _decint(body, pos)
    return offsets


def _decint(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos


def write_chm(project: Project, sources: list[Path], out_path: Path, verbose: bool) -> None:
    pool = StringPool()

    content: list[Entry] = []
    folders: set[str] = set()
    for path in sources:
        rel = path.relative_to(project.root).as_posix().lower()
        # ITSS lookup is case-insensitive but the stored names are not; hhc.exe
        # lowercased everything and topic links in the wild rely on that.
        content.append(Entry("/" + rel, path.read_bytes()))
        # Every directory gets its own zero-length listing entry, as in a CHM
        # hhc.exe produced. Without them the reader cannot enumerate subfolders.
        parts = rel.split("/")[:-1]
        for i in range(len(parts)):
            folders.add("/" + "/".join(parts[: i + 1]) + "/")
    content.extend(Entry(name, b"") for name in sorted(folders))

    # #STRINGS must be populated before it is emitted, so build its consumers first.
    ivb = build_ivb(project.context, pool)
    windows = build_windows(project, pool)
    system = build_system(project, project.language)

    internal = [
        Entry("/#ITBITS", b""),
        Entry("/#SYSTEM", system),
        Entry("/#IVB", ivb),
        Entry("/#STRINGS", bytes(pool.data)),
        Entry("::DataSpace/NameList", build_namelist(["Uncompressed"])),
    ]
    if windows:
        internal.append(Entry("/#WINDOWS", windows))

    entries = [Entry("/", b"")] + internal + content
    entries.sort(key=lambda e: e.name.encode("utf-8"))

    # Lay the content section out, then the directory, then the header.
    blob = bytearray()
    for entry in entries:
        entry.offset = len(blob)
        blob += entry.data
        if len(blob) % 8:
            blob += b"\x00" * (8 - len(blob) % 8)

    directory = build_directory(entries, project.language)

    header_len = 0x60
    section0_off = header_len
    section0_len = 0x18
    dir_off = section0_off + section0_len
    dir_len = len(directory)
    data_off = dir_off + dir_len
    total = data_off + len(blob)

    header = bytearray(header_len)
    header[0:4] = b"ITSF"
    struct.pack_into("<I", header, 0x04, 3)
    struct.pack_into("<I", header, 0x08, header_len)
    struct.pack_into("<I", header, 0x0C, 1)
    struct.pack_into("<I", header, 0x10, int(time.time()) & 0xFFFFFFFF)
    struct.pack_into("<I", header, 0x14, project.language)
    header[0x18:0x28] = GUID_ITSF_A
    header[0x28:0x38] = GUID_ITSF_B
    struct.pack_into("<Q", header, 0x38, section0_off)
    struct.pack_into("<Q", header, 0x40, section0_len)
    struct.pack_into("<Q", header, 0x48, dir_off)
    struct.pack_into("<Q", header, 0x50, dir_len)
    struct.pack_into("<Q", header, 0x58, data_off)

    section0 = bytearray(0x18)
    struct.pack_into("<I", section0, 0x00, 0x01FE)
    struct.pack_into("<I", section0, 0x04, 0)
    struct.pack_into("<Q", section0, 0x08, total)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as fh:
        fh.write(header)
        fh.write(section0)
        fh.write(directory)
        fh.write(blob)

    if verbose:
        print(f"  {len(content)} topics, {len(project.context)} context IDs")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compile an .hhp into a .chm")
    parser.add_argument("project", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="treat project warnings (missing files, unmapped aliases) as errors",
    )
    args = parser.parse_args(argv)

    if not args.project.is_file():
        print(f"chmbuild: no such project: {args.project}", file=sys.stderr)
        return 2

    try:
        project = Project(args.project.resolve())
        sources = gather(project, args.verbose)
        out_path = (args.output or project.output).resolve()
        write_chm(project, sources, out_path, args.verbose)
    except BuildError as exc:
        print(f"chmbuild: {exc}", file=sys.stderr)
        return 1

    for warning in project.warnings:
        print(f"chmbuild: warning: {warning}", file=sys.stderr)
    if args.strict and project.warnings:
        print(f"chmbuild: {len(project.warnings)} warning(s), --strict", file=sys.stderr)
        return 1

    print(f"chmbuild: wrote {out_path} ({out_path.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
