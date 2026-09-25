"""The shipped libraries' dynamic tables: the backend's is the ABI header, libSDL3's the list SDL publishes."""
import os
import pathlib
import re
import subprocess

import pytest

ROOT      = pathlib.Path(__file__).resolve().parents[2]
ABI       = ROOT / 'sources/sdl-rdp/abi/backend.h'
PROTOTYPE = re.compile(r'\b(sdlrdp_\w+)\s*\(')
COUNTER   = re.compile(r'^#define SDLRDP_ABI_VERSION (\d+)$', re.M)
NODE      = re.compile(r'^(\S+) \{$', re.M)
LISTED    = re.compile(r'^\s+(\w+);$', re.M)
# buildutil #177: the build tree has no documented artifact location; these are cmake's binary directories.
BACKEND   = 'sources/sdl-rdp/backend/libbackend.so'
SDL3      = 'sources/sdl-rdp/SDL3/libSDL3.so'


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


def test_backend_exports_the_abi_header_at_its_counter(build):
    header   = ABI.read_text()
    node     = f'BACKEND_{COUNTER.search(header).group(1)}'
    expected = versioned(set(PROTOTYPE.findall(header)), node)
    assert dynamic_table(build / BACKEND) == expected


def test_sdl_exports_its_published_list(build):
    script = (build / 'generated/sdl-rdp-SDL3/exports.map').read_text()
    assert dynamic_table(build / SDL3) == versioned(LISTED.findall(script), NODE.search(script).group(1))
