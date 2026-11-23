"""Guard runtime C/C++/ObjC code against side effects inside assert(), which Android release builds strip via -DNDEBUG."""
from pathlib import Path
import os
import re
import unittest

REPO = Path(__file__).resolve().parents[3]
SCAN_ROOTS = ('dm', 'android')
SKIP_DIRS = frozenset({'.git', '.cxx', 'build', 'gen', 'tests', 'third_party'})
SRC_EXTS = frozenset({'.c', '.h', '.inc', '.cpp', '.cc', '.m', '.mm'})
ALLOWED_CALLS = frozenset({'sizeof', 'offsetof', '__builtin_offsetof', 'atomic_load', 'atomic_load_explicit', 'alignof', '_Alignof'})
TOKEN_RE = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:[^"\\]+|\\.)*"|\'(?:[^\'\\]+|\\.)*\'')
ASSERT_RE = re.compile(r'(?<![A-Za-z0-9_.>])assert\s*\(')
CALL_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*\(')
MUT_RE = re.compile(r'(?:\+\+|--|(?<![=!<>])=(?!=)|[+\-*/%&|^]=|<<=|>>=)')
MAX_EXPR_SCAN = 4096


def strip_literals_and_comments(text: str) -> str:
    """Replace C/C++ comments and literals with whitespace, keeping newlines intact."""
    assert isinstance(text, str) and TOKEN_RE.pattern != ''
    return TOKEN_RE.sub(lambda m: ('\n' * m.group(0).count('\n')) or ' ', text)


def extract_assert_expr(clean: str, open_idx: int) -> str:
    """Return balanced assert(...) argument text starting right after its opening paren."""
    assert 0 <= open_idx <= len(clean)
    depth = 1
    limit = min(len(clean), open_idx + MAX_EXPR_SCAN)
    idx = open_idx
    while idx < limit and depth > 0:
        ch = clean[idx]
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        idx += 1
    assert depth == 0, f'unterminated assert( at offset {open_idx}'
    return clean[open_idx:idx - 1].strip()


def scan_assert_side_effects(text: str, label: str) -> list[str]:
    """Return descriptions of assert(...) calls containing calls or mutations."""
    assert isinstance(label, str) and label != ''
    clean = strip_literals_and_comments(text)
    assert len(clean) > 0 or text == ''
    findings: list[str] = []
    for m in ASSERT_RE.finditer(clean):
        expr = extract_assert_expr(clean, m.end())
        calls = [c for c in CALL_RE.findall(expr) if c not in ALLOWED_CALLS]
        mut = MUT_RE.search(expr) is not None
        if calls or mut:
            line_no = clean.count('\n', 0, m.start()) + 1
            findings.append(f'{label}:{line_no}: assert({expr}) calls={calls} mut={mut}')
    return findings


def iter_runtime_sources(root: Path) -> list[Path]:
    """Collect runtime C/C++/ObjC source paths under dm/ and android/."""
    assert root.is_dir() and len(SCAN_ROOTS) == 2
    files: list[Path] = []
    for top in SCAN_ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for dp, dn, fn in os.walk(base):
            dn[:] = sorted(d for d in dn if d not in SKIP_DIRS)
            for name in sorted(fn):
                if name.startswith('test_') or '_test.' in name:
                    continue
                if Path(name).suffix in SRC_EXTS:
                    files.append(Path(dp) / name)
    assert all(f.suffix in SRC_EXTS for f in files)
    return files


class AssertSideEffects(unittest.TestCase):
    def test_detector_catches_injected_side_effects(self) -> None:
        self.assertEqual(len(scan_assert_side_effects('assert(vka_job_set_handles(&j.jb, h, nh, f) == 0);', 'inj.c')), 1)
        self.assertEqual(len(scan_assert_side_effects('assert(++n > 0);', 'inj.c')), 1)
        self.assertEqual(len(scan_assert_side_effects('assert((rc = foo()) == 0);', 'inj.c')), 1)
        clean_src = 'assert(jb != NULL && idx >= 0);\nassert("// foo()" != NULL);\n_Static_assert(sizeof(int) == 4, "ok");\n'
        self.assertEqual(scan_assert_side_effects(clean_src, 'ok.c'), [])

    def test_repo_runtime_asserts_have_no_side_effects(self) -> None:
        sources = iter_runtime_sources(REPO)
        self.assertGreaterEqual(len(sources), 50)
        findings: list[str] = []
        for path in sources:
            rel = path.relative_to(REPO).as_posix()
            findings.extend(scan_assert_side_effects(path.read_text(encoding='utf-8', errors='replace'), rel))
        self.assertEqual(findings, [], 'side-effecting runtime assert() stripped under -DNDEBUG:\n' + '\n'.join(findings))


if __name__ == '__main__':
    unittest.main()
