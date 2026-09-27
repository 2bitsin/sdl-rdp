#!/usr/bin/env python3
"""includes: a production file outside freerdp-facade includes <freerdp/...> or <winpr/...>, held to facade.baseline.
names: a production file outside the facade names a FreeRDP or WinPR declaration (facade.names) in code, outside a
branch only Windows compiles, where the Win32 names WinPR clones are the platform's own.
facade headers: a header of the facade includes <freerdp/...> or <winpr/...>; tests and benches do not ship."""
import argparse
import collections
import pathlib
import re
import subprocess
import sys

import shape
import spellings

ROOT     = pathlib.Path(__file__).resolve().parents[2]
LINT     = pathlib.Path(__file__).resolve().parent
FACADE   = pathlib.Path('sources/sdl-rdp/freerdp-facade')
PACKAGE  = pathlib.Path('sources/sdl-rdp')
SAMPLE   = pathlib.Path('sources/sample')
RIGS     = ('.test', '.bench')
SECTIONS = ('includes', 'names', 'facade headers')
INCLUDE  = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"](?:freerdp|winpr)/', re.M)
SECTION  = re.compile(r'^\[(.+)\]$')
ENTRY    = re.compile(r'^(\S+): ([1-9]\d*)$')
WINDOWS  = re.compile(r'[ \t]*#[ \t]*(?:ifdef[ \t]+(?:_WIN32|SDL_PLATFORM_WINDOWS)'
                      r'|if[ \t]+defined[ \t]*(?:\([ \t]*(?:_WIN32|SDL_PLATFORM_WINDOWS)[ \t]*\)'
                      r'|[ \t]+(?:_WIN32|SDL_PLATFORM_WINDOWS)))[ \t]*')


def read_names(path):
    """The name column of the table; its first line is the FreeRDP version it came from."""
    return frozenset(line.split('\t', 1)[0] for line in path.read_text().splitlines()[1:])


def sources(root):
    command = ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '--', 'sources']
    listing = subprocess.run(command, cwd=root, capture_output=True, text=True, check=True).stdout.splitlines()
    return sorted(pathlib.Path(name) for name in listing
                  if pathlib.Path(name).suffix in shape.EXTENSIONS and (root / name).is_file())


def production(path):
    """A file that ships: the sample, or the package outside the rigs, `integration/` and every test or bench."""
    if path.is_relative_to(SAMPLE):
        return True
    if not path.is_relative_to(PACKAGE):
        return False
    folders = path.relative_to(PACKAGE).parent.parts
    return (not any(folder == 'integration' or folder.endswith(RIGS) for folder in folders)
            and path.with_suffix('').suffix not in RIGS)


def outside_windows(lexemes):
    """The lexemes outside every `#if` whose one condition is a Windows target; its `#else` is counted again."""
    level = 0
    for token in lexemes:
        directive = shape.DIRECTIVE.match(token.value) if token.value.lstrip().startswith('#') else None
        if level == 0 and directive and WINDOWS.fullmatch(token.value.rstrip('\n')):
            level = 1
        elif level:
            level = shape.disabled_level(directive.group(1) if directive else '', level)
        else:
            yield token


def named(text, names):
    tokens = spellings.code_words(outside_windows(shape.enabled_lexemes(text)))
    return sum(1 for token in tokens if token.value in names)


def counts(root, names):
    """Per section, the count of each file above zero."""
    found = {section: collections.Counter() for section in SECTIONS}
    for relative in sources(root):
        text     = (root / relative).read_text(errors='replace')
        included = len(INCLUDE.findall(text))
        inside   = relative.is_relative_to(FACADE)
        if inside and relative.suffix in ('.h', '.hpp'):
            found['facade headers'][str(relative)] = included
        elif not inside and production(relative):
            found['includes'][str(relative)] = included
            found['names'][str(relative)]    = named(text, names)
    return {section: +counter for section, counter in found.items()}


def read_baseline(path):
    """`[section]` headings over `path: count` lines; any other line fails."""
    baseline, section = {name: collections.Counter() for name in SECTIONS}, None
    for line in filter(str.strip, path.read_text().splitlines()):
        heading, entry = SECTION.match(line), ENTRY.match(line)
        if heading and heading.group(1) in SECTIONS:
            section = heading.group(1)
        elif entry and section:
            baseline[section][entry.group(1)] = int(entry.group(2))
        else:
            raise SystemExit(f'facade: not a section or a `path: count` line: {line}')
    return baseline


def difference(section, path, held, allowed):
    if not allowed and held:
        return [f'{path}: {section} {held}, not in the baseline; a file moved from another path takes that path\'s '
                f'line in facade.baseline, renamed']
    if held > allowed:
        return [f'{path}: {section} {held}, baseline {allowed}']
    if held < allowed:
        return [f'{path}: {section} fell to {held} from {allowed}; run facade.py --update']
    return []


def regressions(found, baseline):
    """A count above its baseline fails; so does one below it, until --update records the fall."""
    return [line for section in SECTIONS for path in sorted(found[section].keys() | baseline[section].keys())
            for line in difference(section, path, found[section][path], baseline[section][path])]


def shrunk(found, baseline):
    """The baseline with every fall recorded and every zero dropped; nothing grows and nothing is added."""
    return {section: +collections.Counter({path: min(count, found[section][path])
                                           for path, count in baseline[section].items()})
            for section in SECTIONS}


def totals(baseline):
    return {section: (len(baseline[section]), sum(baseline[section].values())) for section in SECTIONS}


def summary(before, after):
    """Each section's files and count before and after, the round's done line."""
    return [f'{section}: {files} files, {count} -> {after[section][0]} files, {after[section][1]}'
            for section, (files, count) in before.items()]


def rendered(baseline):
    lines = []
    for section in SECTIONS:
        lines += [f'[{section}]'] + [f'{path}: {count}' for path, count in sorted(baseline[section].items())] + ['']
    return '\n'.join(lines)


def held_baseline(path):
    if not path.exists():
        raise SystemExit(f'facade: no baseline at {path}; it is never re-seeded')
    return read_baseline(path)


def seed(path, found):
    """The one-off that wrote facade.baseline on 10e8cce; it refuses to overwrite one."""
    if path.exists():
        raise SystemExit(f'facade: {path} exists; --seed only writes a baseline that is absent')
    path.write_text(rendered(found))
    return summary(totals({section: collections.Counter() for section in SECTIONS}), totals(found))


def update(path, found):
    before = held_baseline(path)
    after  = shrunk(found, before)
    path.write_text(rendered(after))
    return summary(totals(before), totals(after))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--baseline', type=pathlib.Path, default=LINT / 'facade.baseline')
    parser.add_argument('--names', type=pathlib.Path, default=LINT / 'facade.names')
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument('--update', action='store_true', help='record every fall in the baseline and print totals')
    actions.add_argument('--seed', action='store_true', help='write an absent baseline from today\'s counts, once')
    args  = parser.parse_args(argv)
    found = counts(ROOT, read_names(args.names))
    if args.seed or args.update:
        lines, failed = (seed if args.seed else update)(args.baseline, found), []
    else:
        lines = failed = regressions(found, held_baseline(args.baseline))
    for line in lines:
        print(line)
    return int(bool(failed))


if __name__ == '__main__':
    sys.exit(main())
