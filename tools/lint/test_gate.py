"""The whole-tree lints of the gate, each over the checked-out sources."""
import functools
import os
import pathlib

import pytest

import casts
import clones
import cmake
import columns
import contracts
import facade
import format as formatter
import includes
import namespaces
import pointers
import prefixes
import reserved
import shape
import spellings

LINT = pathlib.Path(__file__).resolve().parent


@pytest.fixture(autouse=True)
def repository_root(monkeypatch):
    monkeypatch.chdir(LINT.parents[1])


def overlapped(selected, worker):
    """Whether jscpd starts ahead of `test_clones`: only when the session runs it and no xdist worker shares it."""
    return 'test_clones' in selected and not worker


@pytest.fixture(scope='module', autouse=True)
def clone_detection(request):
    """jscpd is one single-threaded process over the whole tree, so it runs beside the other lints."""
    if not overlapped({item.name for item in request.session.items}, os.environ.get('PYTEST_XDIST_WORKER')):
        yield clones.main
        return
    with clones.running() as run:
        yield functools.partial(clones.finished, run)


def test_shape():
    assert shape.main(['--allow', str(LINT / 'shape.allow')]) == 0


def test_columns():
    assert columns.main(['--check', 'sources']) == 0


def test_python_columns():
    assert columns.main(['--self-check']) == 0


def test_format():
    assert formatter.main(['--check']) == 0


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


def test_facade():
    assert facade.main([]) == 0


def test_contracts():
    assert contracts.main() == 0


def test_casts():
    assert casts.main() == 0


def test_reserved():
    assert reserved.main() == 0


def test_namespaces():
    assert namespaces.main() == 0


def test_clones(clone_detection):
    assert clone_detection() == 0
