#!/usr/bin/env python3
"""Repository hygiene check, run in CI. Exit 1 on any finding.

Scans every file tracked by git (gzip files decompressed) for content that must not be published:

  operational data   host names, user names, server paths, IP addresses, internal domains, VM ids,
                     run ids and campaign ids of real runs (test fixtures use synthetic ids)
  internal ids       task and audit ids (DL-123, DLN-123, AUDIT-123) anywhere; hardening-round
                     ids (H1, H7) in comments
  dated comments     a YYYY-MM-DD date in a code comment under src/, tests/ or tools/
  attribution        tool attribution lines ("Generated with ...", a tool's Co-Authored-By)
  changelog          CHANGELOG.md structure (Keep a Changelog, Added/Changed/Fixed/Removed only)

  python3 tools/check_repo_hygiene.py [--list-files]
"""
import gzip
import io
import re
import subprocess
import sys
import tokenize
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SELF = "tools/check_repo_hygiene.py"

# --- patterns -------------------------------------------------------------------------------------
OPERATIONAL = [
    ("ipv4 address", re.compile(r"(?<![\d.])(?:10|127|172|192)\.\d{1,3}\.\d{1,3}\.\d{1,3}(?![\d.])")),
    ("home/root path", re.compile(r"(?<![\w.])/(?:home|root|Users)/[A-Za-z_][\w.-]*")),
    ("windows user path", re.compile(r"[A-Za-z]:\\Users\\", re.I)),
    ("vm/container id", re.compile(r"\b(?:VM|CT|vmid)\s?#?\d{3,4}\b")),
    ("internal host", re.compile(r"\b(?:acore-clean|czprox|mgmt-station|prgadm|proxmox|pcbackup)\b", re.I)),
    ("internal domain", re.compile(r"\b[\w-]+\.(?:hungryvessel\.com|local|lan|internal)\b", re.I)),
    ("ssh target", re.compile(r"\bssh\s+[\w.-]+@[\w.-]+")),
    ("run id", re.compile(r"(?<![\w.])\d{15,19}(?![\w.])")),
    ("campaign id", re.compile(r"\bverify-\d{8}-\d{4}-[0-9a-f]{7}\b")),
]
TASK_IDS = re.compile(r"\b(?:DLN?|AUDIT)-\d{1,4}\b")
ROUND_IDS = re.compile(r"\bH\d{1,2}\b")
DATE = re.compile(r"\b20\d\d-[01]\d-[0-3]\d\b")
ATTRIBUTION = [
    re.compile(r"Generated with \[?Claude", re.I),
    re.compile(r"Co-Authored-By:.*\b(?:Claude|Anthropic|Copilot|ChatGPT|OpenAI|Gemini)\b", re.I),
    re.compile(r"noreply@anthropic\.com", re.I),
    re.compile(r"claude\.(?:ai|com)/(?:code|claude-code)", re.I),
]

# Values that look like a finding but are not one: (path, exact text, reason).
ALLOW = [
    ("src/DungeonLead/DungeonLeadKernels.h", "14695981039346656037", "FNV-1a 64 offset basis"),
    ("tests/test_kernels.cpp", "DL-021",
     "inside a test assertion message string; the test sources must stay token-identical"),
] + [(path, value, "synthetic digest in a test")
     for path in ("tests/test_kernels.cpp", "tests/test_run_replay.py")
     for value in ("1111111111111111", "1111111111111112", "2222222222222222", "2222222222222223",
                   "3333333333333333")]

CODE_DIRS = ("src/", "tests/", "tools/")
CPP_EXT = (".cpp", ".h", ".hpp", ".cc")
CHANGELOG_SECTIONS = {"Added", "Changed", "Fixed", "Removed"}


def tracked_files():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], capture_output=True, check=True).stdout
    return [p for p in out.decode().split("\0") if p]


def read_text(path):
    data = (ROOT / path).read_bytes()
    if path.endswith(".gz"):
        data = gzip.decompress(data)
    if b"\0" in data[:4096]:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def allowed(path, text):
    return any(path == p and text == t for p, t, _ in ALLOW)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


# --- comment extraction ---------------------------------------------------------------------------
def cpp_comments(src):
    """(offset, comment text) for // and /* */ comments; literals are skipped."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append((i, src[i:j]))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append((i, src[i:j]))
            i = j
        elif src.startswith('R"', i) and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            m = re.match(r'R"([^ ()\\\t\n]{0,16})\(', src[i:])
            if m:
                end = ")" + m.group(1) + '"'
                j = src.find(end, i + m.end())
                i = n if j < 0 else j + len(end)
            else:
                i += 1
        elif c in "\"'":
            if c == "'" and i > 0 and src[i - 1].isalnum() and i + 1 < n and src[i + 1].isalnum():
                i += 1  # digit separator
                continue
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return out


def python_comments(src):
    """(offset, text) for # comments and docstring-like string statements."""
    out = []
    starts = [0]
    for line in src.splitlines(keepends=True):
        starts.append(starts[-1] + len(line))
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        return [(m.start(), m.group()) for m in re.finditer(r"#.*", src)]
    prev = tokenize.NEWLINE
    for tok in tokens:
        off = starts[tok.start[0] - 1] + tok.start[1]
        if tok.type == tokenize.COMMENT:
            out.append((off, tok.string))
        elif tok.type == tokenize.STRING and prev in (tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT, tokenize.NL):
            out.append((off, tok.string))
        if tok.type not in (tokenize.COMMENT, tokenize.NL):
            prev = tok.type
    return out


def hash_comments(src):
    return [(m.start(), m.group()) for m in re.finditer(r"(?<![\w$])#.*", src)]


def comments(path, src):
    if path.endswith(CPP_EXT):
        return cpp_comments(src)
    if path.endswith(".py"):
        return python_comments(src)
    if path.endswith((".sh", ".yml", ".yaml", ".cmake", ".txt")) or path.endswith("CMakeLists.txt"):
        return hash_comments(src)
    return []


# --- checks ---------------------------------------------------------------------------------------
def check_file(path, src, findings):
    for name, rx in OPERATIONAL:
        for m in rx.finditer(src):
            if not allowed(path, m.group()):
                findings.append((path, line_of(src, m.start()), name, m.group()))
    for m in TASK_IDS.finditer(src):
        if not allowed(path, m.group()):
            findings.append((path, line_of(src, m.start()), "task id", m.group()))
    for rx in ATTRIBUTION:
        for m in rx.finditer(src):
            findings.append((path, line_of(src, m.start()), "attribution", m.group()))
    for off, text in comments(path, src):
        for m in ROUND_IDS.finditer(text):
            findings.append((path, line_of(src, off + m.start()), "round id in comment", m.group()))
        if path.startswith(CODE_DIRS) and not path.startswith("tests/fixtures/"):
            for m in DATE.finditer(text):
                findings.append((path, line_of(src, off + m.start()), "dated comment", m.group()))


def check_changelog(src, findings):
    def bad(line, why):
        findings.append(("CHANGELOG.md", line, "changelog", why))

    lines = src.split("\n")
    if not lines or lines[0] != "# Changelog":
        bad(1, 'first line must be "# Changelog"')
    version = None
    seen_versions, seen_sections = [], set()
    section = None
    for no, line in enumerate(lines, 1):
        if line.startswith("## "):
            m = re.fullmatch(r"## \[(Unreleased|\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?)\](?: - (\d{4}-\d{2}-\d{2}))?", line)
            if not m:
                bad(no, f"bad version heading: {line}")
                continue
            version = m.group(1)
            if version == "Unreleased" and (m.group(2) or seen_versions):
                bad(no, "[Unreleased] must come first and has no date")
            if version != "Unreleased" and not m.group(2):
                bad(no, f"[{version}] needs a date")
            if version in seen_versions:
                bad(no, f"[{version}] appears twice")
            seen_versions.append(version)
            seen_sections, section = set(), None
        elif line.startswith("### "):
            name = line[4:]
            if version is None:
                bad(no, "section before the first version")
            if name not in CHANGELOG_SECTIONS:
                bad(no, f'section "{name}" (allowed: Added, Changed, Fixed, Removed)')
            if name in seen_sections:
                bad(no, f'section "{name}" twice in [{version}]')
            seen_sections.add(name)
            section = name
        elif line.startswith("#") and no > 1:
            bad(no, f"heading level not allowed: {line}")
        elif line.startswith("- "):
            if section is None:
                bad(no, "list item outside a section")
        elif line.startswith((" ", "\t")) and line.strip() and section is not None:
            bad(no, "an item is one line (one sentence)")
    if not seen_versions or seen_versions[0] != "Unreleased":
        bad(1, "missing [Unreleased] as the first version")


def main():
    files = tracked_files()
    if "--list-files" in sys.argv:
        print("\n".join(files))
        return 0
    findings = []
    for path in files:
        if path == SELF or not (ROOT / path).is_file():
            continue
        src = read_text(path)
        if src is None:
            continue
        check_file(path, src, findings)
        if path == "CHANGELOG.md":
            check_changelog(src, findings)
    for path, line, kind, text in sorted(findings):
        print(f"{path}:{line}: {kind}: {text}")
    print(f"{len(files)} files checked, {len(findings)} finding(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
