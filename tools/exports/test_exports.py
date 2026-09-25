"""libSDL3's dynamic table is the list SDL publishes."""
import os
import pathlib
import re
import subprocess

import pytest

NODE   = re.compile(r'^(\S+) \{$', re.M)
LISTED = re.compile(r'^\s+(\w+);$', re.M)
# buildutil #177: the build tree has no documented artifact location; this is cmake's binary directory.
SDL3   = 'sources/sdl-rdp/SDL3/libSDL3.so'


@pytest.fixture(scope='module')
def build():
    if not os.environ.get('BUILDUTIL_BUILD_DIR'):
        pytest.fail('BUILDUTIL_BUILD_DIR is not set; run through `./buildutil test`')
    return pathlib.Path(os.environ['BUILDUTIL_BUILD_DIR'])


def dynamic_table(library: pathlib.Path) -> set[str]:
    listing = subprocess.run(['nm', '-D', '--defined-only', str(library)], check=True, capture_output=True, text=True)
    return {line.split()[-1] for line in listing.stdout.splitlines()}


def versioned(names, node: str) -> set[str]:
    return {f'{name}@@{node}' for name in names} | {node}


def test_sdl_exports_its_published_list(build):
    script = (build / 'generated/sdl-rdp-SDL3/exports.map').read_text()
    assert dynamic_table(build / SDL3) == versioned(LISTED.findall(script), NODE.search(script).group(1))
