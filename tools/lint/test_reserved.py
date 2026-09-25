"""The reserved-identifier lint: `_Upper` names fail but for the ones their owners chose."""
import subprocess

import pytest

import reserved


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    (tmp_path / 'sources/sdl-rdp/video').mkdir(parents=True)
    return tmp_path


def findings(tree, text):
    (tree / 'sources/sdl-rdp/video/a.hpp').write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return [f'{finding.path.name}:{finding.line} {finding.name}' for finding in reserved.findings(tree)]


def test_underscore_capital_names_fail(tree):
    text = 'auto _Load() -> int;\nstatic constexpr bool _Checked = true;\nusing _Stored = int;\n'
    assert findings(tree, text) == ['a.hpp:1 _Load', 'a.hpp:2 _Checked', 'a.hpp:3 _Stored']


def test_lower_members_and_camel_functions_pass(tree):
    assert findings(tree, 'int _value;\nauto Load() -> int;\nauto x = y._value;\n') == []


def test_owner_named_hooks_pass(tree):
    text = ('auto _Label(T*) -> char const*;\nauto _Decode(T&) -> void;\nauto _Encode(T&) -> void;\n'
            'struct _NV_ENC_CONFIG;\nauto page = sysconf(_SC_PAGESIZE);\nauto p = __libc_malloc(8);\n'
            '#define LOG(...) f(__VA_ARGS__)\n#if __cplusplus\n#endif\n')
    assert findings(tree, text) == []


def test_comments_literals_and_includes_pass(tree):
    assert findings(tree, '#include <_Private.h>\n// _Load\nauto a = "_Poll";\n') == []


def test_double_underscores_fail(tree):
    assert findings(tree, 'int value__;\nauto __helper() -> void;\nint a__b;\n') == [
        'a.hpp:1 value__', 'a.hpp:2 __helper', 'a.hpp:3 a__b']


def test_unused_owner_names_are_not_allowed(tree):
    assert findings(tree, 'auto _Export_name = 1;\nauto _Meta() -> int;\nauto _Help() -> int;\n') == [
        'a.hpp:1 _Export_name', 'a.hpp:2 _Meta', 'a.hpp:3 _Help']
