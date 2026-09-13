"""Cross-checks the help wiring: every HELP_ context ID the apps use must be
defined in helpids.h, must resolve to a topic through the .hhp [ALIAS] section,
and every local link in the help must point at a file that exists.

Run from the repository root:  python tools/checkhelp.py
"""

import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HELP = ROOT / "Help"

REF = re.compile(
    r"""(?:href|src)\s*=\s*["']([^"']+)["']"""
    r"""|name\s*=\s*["']Local["']\s+value\s*=\s*["']([^"']+)["']""",
    re.IGNORECASE,
)


def main() -> int:
    hhp = (HELP / "imagewalker.hhp").read_text(encoding="mbcs", errors="replace")
    alias = dict(re.findall(r"^(HELP_\w+)=(\S+)", hhp, re.M))

    defines = dict(
        re.findall(
            r"#define\s+(HELP_\w+)\s+(\S+)",
            (ROOT / "include/iw/helpids.h").read_text(encoding="utf-8"),
        )
    )

    used = collections.defaultdict(set)
    for path in ROOT.glob("src*/**/*"):
        if path.suffix.lower() not in (".cpp", ".h", ".rc"):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r"\bHELP_[A-Z0-9_]+\b", text):
            name = m.group(0)
            # Win32's own WinHelp command codes, not ours.
            if name in ("HELP_FINDER", "HELP_CONTEXT", "HELP_CONTENTS", "HELP_QUIT"):
                continue
            used[name].add(path.parts[len(ROOT.parts)])

    def canonical(name: str) -> str:
        seen = set()
        while name in defines and not defines[name][0].isdigit():
            if name in seen:
                return name
            seen.add(name)
            name = defines[name]
        return name

    problems = 0

    undefined = sorted(n for n in used if n not in defines)
    if undefined:
        problems += len(undefined)
        print("context IDs used by the apps but not defined in helpids.h:")
        for n in undefined:
            print(f"  {n}  used by {sorted(used[n])}")

    unmapped = [
        (n, canonical(n)) for n in sorted(used) if n in defines and canonical(n) not in alias
    ]
    if unmapped:
        problems += len(unmapped)
        print("context IDs that resolve to no topic:")
        for n, c in unmapped:
            print(f"  {n} -> {c}  used by {sorted(used[n])}")

    by_number = collections.defaultdict(set)
    for name, topic in alias.items():
        number = defines.get(canonical(name))
        if number:
            by_number[number].add(topic)
    clashes = {n: t for n, t in by_number.items() if len(t) > 1}
    if clashes:
        problems += len(clashes)
        print("one context ID mapped to more than one topic:")
        for number, topics in sorted(clashes.items()):
            print(f"  {number}: {sorted(topics)}")

    on_disk = {p.name.lower() for p in HELP.rglob("*") if p.is_file()}
    dead = []
    for path in sorted(HELP.rglob("*")):
        if path.suffix.lower() not in (".html", ".hhc", ".hhk"):
            continue
        text = path.read_text(encoding="mbcs", errors="replace")
        for m in REF.finditer(text):
            ref = (m.group(1) or m.group(2)).split("#")[0].strip()
            if not ref or re.match(r"^[a-z][a-z0-9+.-]*:", ref, re.IGNORECASE):
                continue
            if ref.replace("\\", "/").rsplit("/", 1)[-1].lower() not in on_disk:
                dead.append(f"  {path.name}: {ref}")
    if dead:
        problems += len(dead)
        print("links pointing at a file that does not exist:")
        print("\n".join(dead))

    orphans = sorted(
        p.name
        for p in HELP.glob("*.html")
        if not any(
            p.name.lower() in (m.group(1) or m.group(2) or "").lower()
            for q in HELP.rglob("*")
            if q.is_file()
            and q != p
            and q.suffix.lower() in (".html", ".hhc", ".hhk")
            for m in REF.finditer(q.read_text(encoding="mbcs", errors="replace"))
        )
        and p.name not in alias.values()
    )
    if orphans:
        print(f"topics nothing links to: {orphans}")

    print(
        f"{len(alias)} aliases, "
        f"{sum(1 for v in defines.values() if v[0].isdigit())} numeric ids, "
        f"{len(used)} symbols used in app code, "
        f"{len([p for p in HELP.glob('*.html')])} topics"
    )
    print("OK" if problems == 0 else f"{problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
