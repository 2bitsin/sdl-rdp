"""The name table: every FreeRDP and WinPR declaration by name, nothing from system headers, no fields."""
import re

import pytest

import names

FREERDP = '''#pragma once
#include <stddef.h>
#include <winpr/b.h>
#define FREERDP_THING 1
#define FREERDP_CALL(x) (x)
#define _send send
extern "C" {
typedef struct rdp_outer {
    struct rdp_inner { int depth; } inner;
    struct { int anonymous; } unnamed;
    int field;
} rdpOuter;
enum RDP_STATE { RDP_STATE_ACTIVE, RDP_STATE_CLOSED };
int freerdp_call(rdpOuter* outer, int parameter);
extern int freerdp_global;
}
'''
WINPR   = '#pragma once\ntypedef unsigned int UINT32;\ntypedef unsigned char byte;\n'


def include_root(root, version):
    (root / 'freerdp3/freerdp').mkdir(parents=True)
    (root / 'winpr3/winpr').mkdir(parents=True)
    (root / 'freerdp3/freerdp/version.h').write_text(f'#define FREERDP_VERSION_FULL "{version}"\n')
    (root / 'freerdp3/freerdp/a.h').write_text(FREERDP)
    (root / 'winpr3/winpr/b.h').write_text(WINPR)
    return root


@pytest.fixture
def include(tmp_path):
    return include_root(tmp_path / 'p/one/p/include', '3.99.0')


def rows(text):
    return {line.split('\t')[0]: line.split('\t')[1:] for line in text.splitlines()[1:]}


def test_the_first_line_is_the_version(include):
    assert names.generate(include, []).splitlines()[0] == 'FreeRDP 3.99.0'


def test_every_kind_is_named_with_its_header(include):
    assert rows(names.generate(include, [])) == {
        'FREERDP_CALL':         ['macro', 'freerdp/a.h'],
        'FREERDP_THING':        ['macro', 'freerdp/a.h'],
        'FREERDP_VERSION_FULL': ['macro', 'freerdp/version.h'],
        'RDP_STATE':            ['enum', 'freerdp/a.h'],
        'RDP_STATE_ACTIVE':     ['enumerator', 'freerdp/a.h'],
        'RDP_STATE_CLOSED':     ['enumerator', 'freerdp/a.h'],
        'UINT32':               ['typedef', 'winpr/b.h'],
        'freerdp_call':         ['function', 'freerdp/a.h'],
        'freerdp_global':       ['variable', 'freerdp/a.h'],
        'rdpOuter':             ['typedef', 'freerdp/a.h'],
        'rdp_inner':            ['struct', 'freerdp/a.h'],
        'rdp_outer':            ['struct', 'freerdp/a.h']}


def test_member_shaped_and_shared_names_are_left_out(include):
    found = rows(names.generate(include, []))
    assert '_send' not in found
    assert 'byte' not in found


def test_an_error_fails_the_generator(include):
    (include / 'winpr3/winpr/b.h').write_text(WINPR + 'UNKNOWN_TYPE lost;\n')
    with pytest.raises(SystemExit, match="unknown type name 'UNKNOWN_TYPE'"):
        names.generate(include, [])


def test_a_clashing_header_is_parsed_in_its_own_unit(include):
    (include / 'freerdp3/freerdp/crypto').mkdir()
    (include / 'freerdp3/freerdp/crypto/er.h').write_text('#define RDP_STATE_ACTIVE 0x01\n#define ER_ONLY 2\n')
    found = rows(names.generate(include, []))
    assert found['RDP_STATE_ACTIVE'] == ['enumerator,macro', 'freerdp/a.h']
    assert found['ER_ONLY'] == ['macro', 'freerdp/crypto/er.h']


def test_the_cache_is_searched_for_the_required_version(tmp_path):
    include_root(tmp_path / 'p/one/p/include', '3.1.0')
    wanted = include_root(tmp_path / 'p/two/p/include', '3.2.0')
    assert names.freerdp_include(tmp_path, '3.2.0') == wanted


def test_a_locally_built_package_is_found(tmp_path):
    built = include_root(tmp_path / 'p/b/built/p/include', '3.2.0')
    assert names.freerdp_include(tmp_path, '3.2.0') == built


def test_a_locally_built_openssl_is_found(tmp_path):
    built = tmp_path / 'p/b/openssl/p/include'
    (built / 'openssl').mkdir(parents=True)
    (built / 'openssl/ssl.h').write_text('')
    assert names.openssl_includes(tmp_path) == [built]


def test_a_missing_version_names_the_cache(tmp_path):
    include_root(tmp_path / 'p/one/p/include', '3.1.0')
    with pytest.raises(SystemExit, match='no FreeRDP 3.2.0 package'):
        names.freerdp_include(tmp_path, '3.2.0')


def required(root, line):
    (root / 'sources').mkdir()
    (root / 'sources/CMakeLists.txt').write_text(line + '\n')
    return names.required_version(root)


def test_the_required_version_is_the_require_line(tmp_path):
    assert required(tmp_path, 'Require(FreeRDP          VERSION "3.32.0"       CONAN freerdp)') == '3.32.0'


def test_the_required_version_drops_the_fork_release(tmp_path):
    assert required(tmp_path, 'Require(FreeRDP VERSION "3.32.0-sdl-rdp.3" CONAN freerdp)') == '3.32.0'


def test_an_upstream_prerelease_is_kept(tmp_path):
    assert required(tmp_path, 'Require(FreeRDP VERSION "3.32.0-rc1" CONAN freerdp)') == '3.32.0-rc1'


@pytest.mark.parametrize('version', ['3.32.0-sdl-rdp.', '3.32.0-sdl-rdp.x'])
def test_a_malformed_fork_release_names_the_line(tmp_path, version):
    line = f'Require(FreeRDP VERSION "{version}" CONAN freerdp)'
    with pytest.raises(SystemExit, match=re.escape(line)):
        required(tmp_path, line)
