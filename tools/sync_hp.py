#!/usr/bin/env python3
"""Bring every HP figure in docs/*.md into line with the panels.

A panel's width is solved, not typed, so a change to panelkit or to one spec can
move half the family at once -- and every one of those numbers is also written
out in prose, where nothing checks it. This reads the real width out of each
generated `src/<Module>/Panel.hpp` and rewrites the prose to match.

It also keeps the module count, which CLAUDE.md spells out in words
("Thirty-two modules"), equal to the number of modules in plugin.json -- so adding
a module never means remembering to bump it.

README.md is not edited here: tools/readme.py generates it from the same
sources, so its widths, gallery and count cannot disagree with them.

    tools/sync_hp.py            report what disagrees
    tools/sync_hp.py --write    fix it
"""
import glob
import json
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


def whats():
    """slug -> what the module is, from the generated headers (the spec's `what=`)."""
    out = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'src', '*', 'Panel.hpp'))):
        slug = os.path.basename(os.path.dirname(f))
        m = re.search(r'static const char\* const WHAT = "([^"]*)";', open(f).read())
        if m:
            out[slug] = m.group(1)
    return out


#: Words the masthead sets in capitals that are names or acronyms in prose.
_KEEP = {'LFO': 'LFO', 'VCO': 'VCO', 'VCA': 'VCA', 'DAC': 'DAC', 'S&H': 'S&H', 'R2R': 'R2R',
         'PCM': 'PCM', 'DP/4': 'DP/4', 'PT2399': 'PT2399', 'TRIPLE-PT2399': 'triple-PT2399',
         'VIRUS': 'Virus', 'C': 'C', 'NORD': 'Nord', 'LEAD': 'Lead', '2X': '2X',
         'MIDIVERB': 'MIDIverb', '8-CHANNEL': '8-channel', '8-STEP': '8-step', 'REPOSSESSION': 'Repossession'}


def prose(what):
    """The masthead's descriptor as running text: 'MULTIMODE FILTER' -> 'multimode
    filter', keeping the names and acronyms it carries."""
    return ' '.join(_KEEP.get(w, w.lower()) for w in what.split())


def fix_own(text, slug, hp):
    """A manual states its own module's width near the top ('... 12 HP.'). Only
    the first few lines are its header; later 'HP' is HP IN, or another module."""
    if slug not in hp:
        return text, []
    lines = text.split('\n')
    changed = []
    for i in range(min(8, len(lines))):
        def repl(m):
            n = int(m.group(1))
            if n != hp[slug]:
                changed.append((slug, n, hp[slug]))
            return '%d HP' % hp[slug]
        lines[i] = re.sub(r'\b(\d+) HP\b', repl, lines[i])
    return '\n'.join(lines), changed


def fix_titles(text, slug, what):
    """A module manual's H1 says what the module is: '# Toll — struck-metal voice'.
    Anything already after the name (a form number) is kept, after it."""
    if not what:
        return text, None
    lines = text.split('\n', 1)
    h1 = lines[0]
    if not h1.startswith('# '):
        return text, None
    want = prose(what)
    if want in h1:
        return text, None
    name, _, rest = h1[2:].partition(' — ')
    new = '# %s — %s' % (name.strip(), want) + (' · %s' % rest.strip() if rest.strip() else '')
    return '\n'.join([new] + lines[1:]), (h1, new)


_ONES = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten',
         'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen',
         'eighteen', 'nineteen']
_TENS = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety']


def words(n):
    """1..99 in English, hyphenated: 32 -> 'thirty-two'."""
    if n < 20:
        return _ONES[n]
    return _TENS[n // 10] + ('-' + _ONES[n % 10] if n % 10 else '')


def module_count():
    return len(json.load(open(os.path.join(ROOT, 'plugin.json')))['modules'])


#: Where the count is written: (file, regex whose group 1 is the number word).
COUNTS = {
    'CLAUDE.md': [r'\*\*Taxxess\*\*\. ([A-Za-z-]+) modules sharing', r'and the ([a-z-]+) module slugs',
                  r'reaches all ([a-z-]+)\.'],
}


def fix_counts(text, name):
    """Rewrite the spelled-out module count; returns (text, [(was, now)])."""
    now, changed = words(module_count()), []
    for pat in COUNTS.get(name, []):
        def repl(m):
            was = m.group(1)
            new = now.capitalize() if was[:1].isupper() else now
            if was != new:
                changed.append((was, new))
            return m.group(0)[:m.start(1) - m.start(0)] + new + m.group(0)[m.end(1) - m.start(0):]
        text = re.sub(pat, repl, text, flags=re.M)
    return text, changed


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
    # README.md is generated by tools/readme.py, which reads these same numbers.
    files = [os.path.join(ROOT, 'CLAUDE.md')]
    files += sorted(glob.glob(os.path.join(ROOT, 'docs', '*.md')))
    total = 0
    for f in files:
        src = open(f).read()
        out, cchanged = fix_counts(src, os.path.basename(f))
        for was, now in cchanged:
            print('  %-22s module count: %s -> %s' % (os.path.relpath(f, ROOT), was, now))
        out, changed = fix(out, hp)
        own_slug = os.path.basename(f)[:-3]
        out, own = fix_own(out, own_slug, hp) if f.startswith(os.path.join(ROOT, 'docs')) else (out, [])
        changed = list(changed) + own
        for slug, was, now in changed:
            print('  %-22s %s: %d -> %d HP' % (os.path.relpath(f, ROOT), slug, was, now))
        slug = os.path.basename(f)[:-3]
        out, tchanged = fix_titles(out, slug, whats().get(slug))
        if tchanged:
            print('  %-22s title: %s -> %s' % (os.path.relpath(f, ROOT), tchanged[0], tchanged[1]))
            cchanged = list(cchanged) + [tchanged]
        out, wchanged = fix_widths(out, hp)
        for slug, was, now in wchanged:
            print('  %-22s %s: gallery image %d -> %d px' %
                  (os.path.relpath(f, ROOT), slug, was, now))
        total += len(changed) + len(wchanged) + len(cchanged)
        if (changed or wchanged or cchanged) and write:
            open(f, 'w').write(out)
    if not total:
        print('  every HP figure, gallery width and the module count already match')
    elif not write:
        print('\n  %d disagree; run with --write to fix' % total)
    return 1 if (total and not write) else 0


if __name__ == '__main__':
    raise SystemExit(main())
