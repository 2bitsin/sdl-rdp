"""The include lint: a header of another module needs that module in Link_dependencies."""
import pytest

import includes


@pytest.fixture
def tree(tmp_path):
    for name, text in {'sources/lib/a/CMakeLists.txt': 'Init_submodule()\n',
                       'sources/lib/a/a.hpp': '#pragma once\n',
                       'sources/app/CMakeLists.txt': 'Init_submodule()\nLink_dependencies(other TEST a)\n',
                       'sources/rig/CMakeLists.txt': 'Init_submodule()\nLink_dependencies(BENCH a)\n'}.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text)
    return tmp_path


def findings(tree, name, text):
    (tree / name).write_text(text)
    return [f'{finding.path}:{finding.line} {finding.module}' for finding in includes.findings(tree)]


def test_unlinked_include_fails(tree):
    assert findings(tree, 'sources/app/main.cpp', '#include <vector>\n#include <lib/a/a.hpp>\n') == [
        'sources/app/main.cpp:2 sources/lib/a']


def test_test_dependency_serves_test_files(tree):
    assert findings(tree, 'sources/app/main.test.cpp', '#include <lib/a/a.hpp>\n') == []


def test_bench_dependency_serves_bench_files_and_subtrees(tree):
    (tree / 'sources/rig/support.bench').mkdir()
    assert findings(tree, 'sources/rig/cost.bench.cpp', '#include <lib/a/a.hpp>\n') == []
    assert findings(tree, 'sources/rig/support.bench/session.hpp', '#include <lib/a/a.hpp>\n') == []


def test_bench_dependency_serves_neither_test_nor_library_files(tree):
    (tree / 'sources/rig/cost.test.cpp').write_text('#include <lib/a/a.hpp>\n')
    assert findings(tree, 'sources/rig/main.cpp', '#include <lib/a/a.hpp>\n') == [
        'sources/rig/cost.test.cpp:1 sources/lib/a', 'sources/rig/main.cpp:1 sources/lib/a']


def test_positional_dependency_serves_every_file(tree):
    (tree / 'sources/app/CMakeLists.txt').write_text('Init_submodule()\nLink_dependencies(lib-a)\n')
    assert findings(tree, 'sources/app/main.cpp', '#include <lib/a/a.hpp>\n') == []


def test_own_headers_and_external_headers_pass(tree):
    assert findings(tree, 'sources/lib/a/a.cpp', '#include <lib/a/a.hpp>\n#include <gtest/gtest.h>\n') == []
