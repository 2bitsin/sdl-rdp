"""The CMake lint: each declaration in its place, the scaffolded root, one ticket per exempted call."""
import subprocess

import pytest

import cmake

SCAFFOLD = 'cmake_minimum_required(VERSION 3.25)\nproject(p CXX)\ninclude(buildutil)\nadd_subdirectory(sources)\n'


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    (tmp_path / 'buildutil.toml').write_text('[project]\nname = "p"\n')
    (tmp_path / 'CMakeLists.txt').write_text(SCAFFOLD)
    return tmp_path


def lint(tree, text, name='sources/m/CMakeLists.txt'):
    path = tree / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return [cmake.describe(verdict) for path in cmake.tracked(tree) for verdict in cmake.verdicts(tree, path)]


def test_module_declarations_pass(tree):
    assert lint(tree, 'Init_submodule()\nLink_dependencies(\n  core # the facade\n  TEST gtest)\n') == []


def test_sources_declarations_pass(tree):
    assert lint(tree, 'Require(GTest VERSION ">=1" TEST)\nScan_subdirectories()\n', 'sources/CMakeLists.txt') == []


def test_foreign_command_fails(tree):
    assert lint(tree, 'Init_submodule()\nset(X "a(b)")\n') == [
        'sources/m/CMakeLists.txt:2: set is not a buildutil declaration here']


def test_require_fails_in_a_module(tree):
    assert lint(tree, 'Require(GTest)\n') == ['sources/m/CMakeLists.txt:1: Require is not a buildutil declaration here']


def test_module_command_fails_in_sources(tree):
    assert lint(tree, 'Init_submodule()\n', 'sources/CMakeLists.txt') == [
        'sources/CMakeLists.txt:1: Init_submodule is not a buildutil declaration here']


def test_cmake_file_outside_sources_allows_nothing(tree):
    assert lint(tree, 'Init_submodule()\n', 'cmake/extra.cmake') == [
        'cmake/extra.cmake:1: Init_submodule is not a buildutil declaration here']


def test_scaffolded_root_passes(tree):
    assert lint(tree, SCAFFOLD, 'CMakeLists.txt') == []


def test_root_argument_differs(tree):
    assert lint(tree, SCAFFOLD.replace('p CXX', 'p C CXX'), 'CMakeLists.txt') == [
        'CMakeLists.txt:2: project differs from the scaffolded `project(p CXX)`']


def test_root_extra_call_fails(tree):
    assert lint(tree, SCAFFOLD + 'enable_testing()\n', 'CMakeLists.txt') == [
        'CMakeLists.txt:5: enable_testing is not a buildutil declaration']


def test_root_missing_statement_fails(tree):
    assert lint(tree, SCAFFOLD.replace('add_subdirectory(sources)\n', ''), 'CMakeLists.txt') == [
        'CMakeLists.txt:0: add_subdirectory is missing; the scaffold has `add_subdirectory(sources)`']


def test_root_exemption_passes(tree):
    text = SCAFFOLD.replace('project(p CXX)', '# buildutil #9: reason.\nproject(p C CXX)')
    assert lint(tree, text, 'CMakeLists.txt') == ['CMakeLists.txt:3: project exempted by buildutil #9']


def test_one_heading_exempts_one_call(tree):
    text = 'Init_submodule()\n# buildutil #7: reason.\nset(A\n  1)\nset(B 2)\n'
    assert lint(tree, text) == ['sources/m/CMakeLists.txt:3: set exempted by buildutil #7',
                                'sources/m/CMakeLists.txt:5: set is not a buildutil declaration here']


def test_exemption_needs_a_ticket(tree):
    assert lint(tree, '# buildutil: someday.\nset(A 1)\n') == [
        'sources/m/CMakeLists.txt:2: set is exempted without a ticket number']


def test_commented_calls_are_not_calls(tree):
    assert lint(tree, '# set(A 1)\n#[[\nset(B 2)\n]]\nInit_submodule()\n') == []


def test_underscore_directories_are_skipped(tree):
    assert lint(tree, 'set(A 1)\n', name='_build/CMakeLists.txt') == []


def test_lint_fails_on_a_violation(tree):
    lint(tree, 'set(A 1)\n')
    assert not cmake.lint(tree)
