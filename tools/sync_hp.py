#!/usr/bin/env python3
"""Bring every HP figure in README.md and docs/*.md into line with the panels.

A panel's width is solved, not typed, so a change to panelkit or to one spec can
move half the family at once -- and every one of those numbers is also written
out in prose, where nothing checks it. This reads the real width out of each
generated `src/<Module>/Panel.hpp` and rewrites the prose to match.

It does the README gallery's image widths too. Those are the same fact written a
third way, and the one nobody thinks to update: four had drifted before this
checked them, one of them showing an 18 HP panel at the size of a 10 HP one for
however many sessions it had been since that module grew.

    tools/sync_hp.py            report what disagrees
    tools/sync_hp.py --write    fix it
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def widths():
    """slug -> HP, from the generated headers."""
    out = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'src', '*', 'Panel.hpp'))):
        slug = os.path.basename(os.path.dirname(f))
        m = re.search(r'static const int\s+HP = (\d+);', open(f).read())
        if m:
            out[slug] = int(m.group(1))
    return out


#: How a module's name is written in prose, where that differs from its slug.
SPACED = {
    'AuditLogic': 'Audit Logic', 'PatchAudit': 'Patch Audit',
    'PaymentSchedule': 'Payment Schedule', 'ScheduleA': 'Schedule A',
    'SignHere': 'Sign Here', 'SixFigures': 'Six Figures',
    'TaxBracket': 'Tax Bracket', 'UncertaintyPolicy': 'Uncertainty Policy',
}


#: Pixels per HP in the README gallery's <img width="...">. Every entry that
#: had not drifted agreed on this, which is what makes it the convention rather
#: than a guess.
PX_PER_HP = 8.4


def fix_widths(text, hp):
    """Rewrite every gallery image width to match its panel."""
    changed = []

    def repl(m):
        slug, n = m.group('slug'), int(m.group('n'))
        want = hp.get(slug)
        if want is None:
            return m.group(0)
        px = int(round(want * PX_PER_HP))
        if px == n:
            return m.group(0)
        changed.append((slug, n, px))
        return m.group(0).replace('width="%d"' % n, 'width="%d"' % px)

    pat = re.compile(r'previews/(?P<slug>\w+)\.png" width="(?P<n>\d+)"')
    return pat.sub(repl, text), changed


def fix(text, hp):
    """Rewrite every '<link to a module> ... N HP' in `text`."""
    changed = []

    def repl(m):
        slug, mid, n = m.group('slug'), m.group('mid'), int(m.group('n'))
        want = hp.get(slug)
        if want is None or want == n:
            return m.group(0)
        changed.append((slug, n, want))
        return m.group(0).replace('%d HP' % n, '%d HP' % want)

    # "[Anything](docs/Slug.md)** · 12 HP" and "— 12 HP ·", in either order,
    # with at most a short run of markdown between the link and the number.
    pat = re.compile(r'\(docs/(?P<slug>\w+)\.md\)(?P<mid>[^|\n]{0,12}?)(?P<n>\d+) HP')
    text = pat.sub(repl, text)
    return text, changed


def main():
    write = '--write' in sys.argv
    hp = widths()
    files = [os.path.join(ROOT, 'README.md')]
    files += sorted(glob.glob(os.path.join(ROOT, 'docs', '*.md')))
    total = 0
    for f in files:
        src = open(f).read()
        out, changed = fix(src, hp)
        for slug, was, now in changed:
            print('  %-22s %s: %d -> %d HP' % (os.path.relpath(f, ROOT), slug, was, now))
        out, wchanged = fix_widths(out, hp)
        for slug, was, now in wchanged:
            print('  %-22s %s: gallery image %d -> %d px' %
                  (os.path.relpath(f, ROOT), slug, was, now))
        changed += wchanged
        total += len(changed)
        if changed and write:
            open(f, 'w').write(out)
    if not total:
        print('  every HP figure and gallery width already matches the panels')
    elif not write:
        print('\n  %d disagree; run with --write to fix' % total)
    return 1 if (total and not write) else 0


if __name__ == '__main__':
    raise SystemExit(main())
