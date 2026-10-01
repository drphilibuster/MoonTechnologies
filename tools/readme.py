#!/usr/bin/env python3
"""Write README.md from the things that are facts, plus the prose that is not.

A README that spells out how many modules there are, how wide each is and which
platforms are built goes stale the first time any of those moves, and nothing
notices. So none of them is typed. This reads:

    plugin.json                the modules, their display names and tags
    src/<Module>/Panel.hpp     each panel's solved width in HP
    .github/workflows/release.yml   the platforms that get a build
    tools/readme/README.in.md  the surrounding prose, with {{markers}}
    tools/readme/modules.md    each module's group, credit line and blurb

and writes README.md. A module in plugin.json with no block in modules.md (or the
reverse) is an error rather than a silent omission, so adding a module cannot
leave the README one short.

    tools/readme.py            rewrite README.md
    tools/readme.py --check    exit 1 if README.md is not what this would write
"""
import difflib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sync_hp import widths, words, PX_PER_HP, SPACED  # noqa: E402

SRC = os.path.join(ROOT, 'tools', 'readme')

#: Group keys in README order are fixed by the template; this is only validation.
GROUPS = ('built', 'hardware', 'video', 'miaw', 'grew')

#: Most tiles in a gallery row, and the most HP a row may hold before it wraps.
ROW_TILES, ROW_HP = 4, 100


def parse_entries(text):
    """modules.md -> ordered {slug: {group, credit, body}}."""
    text = re.sub(r'<!--.*?-->', '', text, flags=re.S)
    out = {}
    for block in re.split(r'^## ', text, flags=re.M)[1:]:
        head, _, rest = block.partition('\n')
        slug, meta = head.strip(), {}
        lines = rest.split('\n')
        while lines and re.match(r'^(group|credit): ', lines[0]):
            k, _, v = lines.pop(0).partition(': ')
            meta[k] = v
        if slug in out:
            sys.exit('modules.md: %s appears twice' % slug)
        if meta.get('group') not in GROUPS:
            sys.exit('modules.md: %s needs group: %s' % (slug, ' | '.join(GROUPS)))
        out[slug] = dict(meta, body='\n'.join(lines).strip())
    return out


def platforms():
    """The release matrix's targets, as a spoken list: `a`, `b` and `c`."""
    text = open(os.path.join(ROOT, '.github', 'workflows', 'release.yml')).read()
    seen = []
    for t in re.findall(r'target:\s*([a-z]+-[a-z0-9]+)', text):
        if t not in seen:
            seen.append(t)
    ticks = ['`%s`' % t for t in seen]
    return ', '.join(ticks[:-1]) + ' and ' + ticks[-1] if len(ticks) > 1 else ticks[0]


def gallery(mods, hp):
    """Rows of tiles, packed by panel width, in README order."""
    rows, row, used = [], [], 0
    for m in mods:
        w = hp[m['slug']]
        if row and (len(row) == ROW_TILES or used + w > ROW_HP):
            rows.append(row)
            row, used = [], 0
        row.append(m)
        used += w
    if row:
        rows.append(row)
    out = []
    for r in rows:
        img = ' | '.join('<img src="tools/previews/%s.png" width="%d">'
                         % (m['slug'], int(round(hp[m['slug']] * PX_PER_HP))) for m in r)
        cap = ' | '.join('**[%s](docs/%s.md)**' % (m['name'], m['slug']) for m in r)
        out.append('| %s |\n|%s\n| %s |' % (img, '---|' * len(r), cap))
    return '\n\n'.join(out)


def entry(m, hp):
    head = '### [%s](docs/%s.md) — %d HP · *%s*' % (m['name'], m['slug'], hp[m['slug']],
                                                  ', '.join(m['tags']))
    parts = [head]
    if m.get('credit'):
        parts.append(m['credit'])
    parts.append(m['body'])
    return '\n\n'.join(parts)


def build():
    manifest = json.load(open(os.path.join(ROOT, 'plugin.json')))['modules']
    entries = parse_entries(open(os.path.join(SRC, 'modules.md')).read())
    hp = widths()

    slugs = [m['slug'] for m in manifest]
    problems = ['plugin.json has %s but tools/readme/modules.md has no block for it' % s
                for s in slugs if s not in entries]
    problems += ['tools/readme/modules.md describes %s, which is not in plugin.json' % s
                 for s in entries if s not in slugs]
    problems += ['%s has no generated src/%s/Panel.hpp, so its width is unknown' % (s, s)
                 for s in slugs if s not in hp]
    if problems:
        sys.exit('\n'.join(problems))

    by_slug = {m['slug']: m for m in manifest}
    mods = [dict(by_slug[s], **entries[s], name=SPACED.get(s, by_slug[s]['name']))
            for s in entries]                                        # modules.md order

    text = open(os.path.join(SRC, 'README.in.md')).read()
    text = re.sub(r'<!--.*?-->\n', '', text, count=1, flags=re.S)
    n = words(len(slugs))
    text = text.replace('{{Count}}', n.capitalize()).replace('{{count}}', n)
    text = text.replace('{{platforms}}', platforms())
    text = text.replace('{{gallery}}', gallery(mods, hp))
    for g in GROUPS:
        text = text.replace('{{group:%s}}' % g,
                            '\n\n'.join(entry(m, hp) for m in mods if m['group'] == g))
    left = re.findall(r'\{\{[^}]*\}\}', text)
    if left:
        sys.exit('README.in.md has markers nothing fills: %s' % ', '.join(left))
    banner = ('<!-- GENERATED by tools/readme.py from plugin.json, the panel headers and\n'
              '     tools/readme/. Edit those and run `make readme`; CI fails if this\n'
              '     file is not what the generator writes. -->\n')
    return banner + text


def main():
    out = build()
    path = os.path.join(ROOT, 'README.md')
    have = open(path).read() if os.path.exists(path) else ''
    if '--check' in sys.argv:
        if have == out:
            print('README.md is up to date')
            return 0
        sys.stdout.writelines(difflib.unified_diff(have.splitlines(True), out.splitlines(True),
                                                   'README.md', 'generated'))
        print('\nREADME.md is out of date; run `make readme`')
        return 1
    open(path, 'w').write(out)
    print('wrote README.md')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
