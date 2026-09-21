#!/usr/bin/env python3
"""Rebuild docs/test-bench.html from docs/SYSTEM_TEST_GUIDE.md.

The bench is a click-through version of the manual test guide: every test
gets pass / fail / n-a buttons and a comments box, and it produces a
paste-able sign-off report. It is PUBLISHED as a Claude artifact, where the
marks persist in that artifact's own store:

    https://claude.ai/artifact/LLLB9bmgk1dhRhW4MCKJoP

The guide is the single source of truth. Tests are parsed out of its markdown
tables rather than retyped, so the two can never drift. Run this after
changing the guide, then republish the artifact.

    python3 tools/build_test_bench.py
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GUIDE = ROOT / "docs" / "SYSTEM_TEST_GUIDE.md"
BENCH = ROOT / "docs" / "test-bench.html"

# "## 9. Style zones **[NOW]** — Task 8"
SECTION_RE = re.compile(r"^## (\d+)\.\s+(.*)$")
# "| T8.2 | do this | expect that |" — S / T / X / H / G prefixes, dot optional
TEST_RE = re.compile(r"^\|\s*((?:S|T|X|H|G)\.?[0-9][0-9A-Za-z.]*)\s*\|(.*)\|(.*)\|\s*$")


def strip_markdown(text):
    text = re.sub(r"`(.+?)`", r"\1", text)
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = re.sub(r"\*(.+?)\*", r"\1", text)
    return text.strip()


def parse_guide(markdown):
    sections, current = [], None
    for line in markdown.splitlines():
        heading = SECTION_RE.match(line)
        if heading:
            title = heading.group(2)
            if "PENDING" in title and "NOW" in title:
                status = "partial"
            elif "PENDING" in title:
                status = "pending"
            else:
                status = "now"
            clean = re.sub(
                r"\*\*|\[NOW in Rack, PENDING on hardware\]|\[NOW\]|\[PENDING[^\]]*\]",
                "",
                title,
            )
            clean = re.sub(r"\s+", " ", clean.replace("—", "–")).strip(" –")
            current = {"n": int(heading.group(1)), "title": clean,
                       "status": status, "tests": []}
            sections.append(current)
            continue
        if current is None:
            continue
        test = TEST_RE.match(line)
        if test:
            current["tests"].append({
                "id": test.group(1).strip(),
                "do": strip_markdown(test.group(2)),
                "expect": strip_markdown(test.group(3)),
            })
    return [s for s in sections if s["tests"]]


def main():
    if not BENCH.exists():
        sys.exit("docs/test-bench.html is missing — it holds the page itself, "
                 "not just the data.")

    sections = parse_guide(GUIDE.read_text())
    total = sum(len(s["tests"]) for s in sections)
    payload = json.dumps(sections, separators=(",", ":"), ensure_ascii=False)

    html = BENCH.read_text()
    html, n = re.subn(
        r'(<script id="test-data" type="application/json">).*?(</script>)',
        lambda m: m.group(1) + payload + m.group(2),
        html,
        flags=re.S,
    )
    if n != 1:
        sys.exit("could not find the test-data script block in the bench page")

    # keep the pre-JS fallbacks honest
    html = re.sub(r'<b id="ct">\d+ tests</b>', f'<b id="ct">{total} tests</b>', html)
    html = re.sub(r'<b id="n-left">\d+</b>', f'<b id="n-left">{total}</b>', html)

    BENCH.write_text(html)
    print(f"{BENCH.relative_to(ROOT)}: {len(sections)} sections, {total} tests")
    for s in sections:
        print(f"  {s['n']:>2}. {s['title']}  ({len(s['tests'])})")


if __name__ == "__main__":
    main()
