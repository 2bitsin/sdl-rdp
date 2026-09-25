#!/usr/bin/env python3
"""Refuse a file whose name repeats the stem of the folder holding it."""
import pathlib
import re
import sys

ROOT  = pathlib.Path(__file__).resolve().parents[2]
CLASS = re.compile(r'^(?:class|struct)\s+(\w+)(?:\s+final)?\s*[:{]', re.M)


def stem(directory):
    """The name a `<name>/`, `<name>.test/` or `<name>.bench/` directory answers to."""
    return directory.name.removesuffix('.test').removesuffix('.bench')


def stems(directory):
    """A private `_<name>/` folder speaks for the folder holding it, so both names count."""
    return (stem(directory), *(stems(directory.parent) if directory.name.startswith('_') else ()))


def prefixes(directory):
    """`<dir>-`, and SDL's `SDL_<dir>` driver file prefix, for each name the folder answers to."""
    return tuple(prefix for name in stems(directory) for prefix in (f'{name}-', f'SDL_{name}'))


def names_its_class(path):
    """A namesake `<dir>/<dir>.<ext>` stays when `<dir>.hpp` defines the class the directory is named for."""
    header = path.with_suffix('.hpp')
    return header.is_file() and stem(path.parent) in {name.lower() for name in CLASS.findall(header.read_text())}


def repeats(path):
    namesake = path.stem in stems(path.parent) and not names_its_class(path)
    return path.name.startswith(prefixes(path.parent)) or namesake


def findings(root):
    """Each file under sources/ that repeats the stem of its folder, namesakes included, sorted."""
    return [path.relative_to(root) for path in sorted((root / 'sources').rglob('*'))
            if path.is_file() and path.name != 'CMakeLists.txt' and repeats(path)]


def main():
    found = findings(ROOT)
    for path in found:
        print(f'{path}: repeats {stem(path.parent)}, the directory already says it; name the file for what it holds')
    return int(bool(found))


if __name__ == '__main__':
    sys.exit(main())
