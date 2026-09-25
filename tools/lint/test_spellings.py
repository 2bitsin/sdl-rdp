"""The spelling lint: cstdint spellings everywhere, WinPR names nowhere but the facade's boundary ones."""
import subprocess

import pytest

import spellings


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    (tmp_path / 'test_package').mkdir()
    (tmp_path / 'sources/sdl-rdp/freerdp-facade').mkdir(parents=True)
    (tmp_path / 'sources/sdl-rdp/video').mkdir(parents=True)
    return tmp_path


def findings(tree, name, text):
    (tree / name).write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return [f'{finding.path.name}:{finding.line} {finding.spelling}' for finding in spellings.findings(tree)]


def test_winpr_scalars_fail_in_project_code(tree):
    assert findings(tree, 'sources/sdl-rdp/video/a.hpp', 'auto F(BYTE a, UINT16 b) -> BOOL;\nDWORD c;\n') == [
        'a.hpp:1 BYTE', 'a.hpp:1 UINT16', 'a.hpp:1 BOOL', 'a.hpp:2 DWORD']


def test_builtin_spellings_fail(tree):
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', 'unsigned a;\nlong b;\nsize_t c;\nstd::atomic_uint d;\n') == [
        'a.cpp:1 unsigned', 'a.cpp:2 long', 'a.cpp:3 size_t', 'a.cpp:4 atomic_uint']


def test_cstdint_spellings_pass(tree):
    text = 'std::size_t a;\nstd::uint32_t b;\nstd::int8_t c;\nint d;\nauto e = x.LONG;\n'
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', text) == []


def test_comments_and_literals_pass(tree):
    text = '// BYTE and unsigned\nauto a = "UINT32 long";\n/* DWORD */\nauto b = \'L\';\n'
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', text) == []


def test_directive_bodies_are_code(tree):
    text = '#include <winpr/wtypes.h>\n#define WIDTH unsigned\n#if 0\nBYTE hidden;\n#endif\n'
    assert findings(tree, 'sources/sdl-rdp/video/a.hpp', text) == ['a.hpp:2 unsigned']


def test_facade_keeps_handle_only(tree):
    text = 'HANDLE a = nullptr;\nauto b = TRUE;\nUINT32 c;\n'
    assert findings(tree, 'sources/sdl-rdp/freerdp-facade/a.cpp', text) == ['a.cpp:2 TRUE', 'a.cpp:3 UINT32']


def test_bare_fixed_width_names_fail(tree):
    text = 'uint32_t a;\nstd::int16_t b;\n::uint8_t c;\nauto d = p->uint64_t;\n'
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', text) == ['a.cpp:1 uint32_t', 'a.cpp:3 uint8_t',
                                                                   'a.cpp:4 uint64_t']


def test_platform_spellings_fail(tree):
    text = 'short a;\nsigned char b;\nssize_t c;\nwchar_t d;\nSECURITY_STATUS e;\nULONG_PTR f;\n'
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', text) == [
        'a.cpp:1 short', 'a.cpp:2 signed', 'a.cpp:3 ssize_t', 'a.cpp:4 wchar_t', 'a.cpp:5 SECURITY_STATUS',
        'a.cpp:6 ULONG_PTR']


def test_test_package_is_scanned(tree):
    assert findings(tree, 'test_package/smoke.cpp', 'auto a = (long long)port;\n') == [
        'smoke.cpp:1 long', 'smoke.cpp:1 long']


def test_boundary_names_fail_outside_the_facade(tree):
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', 'HANDLE a;\nauto b = FALSE;\n') == [
        'a.cpp:1 HANDLE', 'a.cpp:2 FALSE']


def test_sdl_scalars_fail_like_the_winpr_ones(tree):
    text = 'Uint8 a;\nSint16 b;\nauto c = SDL_GetTicks() + Uint64{ 1 };\nauto d = event.Uint32;\n'
    assert findings(tree, 'sources/sdl-rdp/video/a.cpp', text) == ['a.cpp:1 Uint8', 'a.cpp:2 Sint16', 'a.cpp:3 Uint64']


def test_ignored_files_are_not_scanned(tree):
    (tree / 'test_package/build').mkdir()
    (tree / 'test_package/build/generated.cpp').write_text('unsigned a;\n')
    (tree / '.gitignore').write_text('test_package/build/\n')
    assert findings(tree, 'test_package/smoke.cpp', 'int a;\n') == []


def test_untracked_new_files_are_scanned(tree):
    (tree / 'sources/sdl-rdp/video/new.cpp').write_text('unsigned a;\n')
    assert [f'{finding.path.name}:{finding.line} {finding.spelling}' for finding in spellings.findings(tree)] == [
        'new.cpp:1 unsigned']
