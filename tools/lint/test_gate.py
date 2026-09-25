"""The whole-tree lints of the gate, each over the checked-out sources."""
import pathlib

import pytest

import clones
import cmake
import columns
import format as formatter
import includes
import pointers
import prefixes
import shape
import spellings

LINT = pathlib.Path(__file__).resolve().parent


@pytest.fixture(autouse=True)
def repository_root(monkeypatch):
    monkeypatch.chdir(LINT.parents[1])


def test_shape():
    assert shape.main(['--allow', str(LINT / 'shape.allow')]) == 0


def test_columns():
    assert columns.main(['--check', 'sources']) == 0


def test_python_columns():
    assert columns.main(['--self-check']) == 0


def test_format():
    assert formatter.main(['--check']) == 0


def test_clones():
    assert clones.main() == 0


def test_cmake():
    assert cmake.main() == 0


def test_includes():
    assert includes.main() == 0


def test_spellings():
    assert spellings.main() == 0


def test_prefixes():
    assert prefixes.main() == 0


def test_pointers():
    assert pointers.main(['--baseline', str(LINT / 'pointers.baseline')]) == 0
