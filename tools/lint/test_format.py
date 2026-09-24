"""The format fixture is a fixed point, its unaligned copy formats back to it, and a second pass is idle."""
import pathlib
import re
import shutil

import pytest

import format as formatter

FIXTURE = pathlib.Path(__file__).with_name('fixtures') / 'format-fixture.cpp'


@pytest.fixture
def executable():
    found = formatter.clang_format()
    if found is None:
        pytest.fail('clang-format 20 is not installed')
    return found


def formatted(executable, directory):
    formatter.format_tree(executable, directory)
    return (directory / 'fixture.cpp').read_text()


def test_fixture_is_a_fixed_point(executable, tmp_path):
    shutil.copy(FIXTURE, tmp_path / 'fixture.cpp')
    assert formatted(executable, tmp_path) == FIXTURE.read_text()


def test_unaligned_copy_formats_back_and_stays(executable, tmp_path):
    unaligned = re.sub(r'([^ ])  +', r'\1 ', FIXTURE.read_text())
    assert unaligned != FIXTURE.read_text()
    (tmp_path / 'fixture.cpp').write_text(unaligned)
    assert formatted(executable, tmp_path) == FIXTURE.read_text()
    assert formatted(executable, tmp_path) == FIXTURE.read_text()
