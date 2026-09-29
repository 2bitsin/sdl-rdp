"""The fan: per-item work over worker processes in item order, and every fanned lint equal in process or pooled."""
import concurrent.futures
import contextlib
import io
import json
import math
import operator
import re
import subprocess
import threading

import pytest

import casts
import columns
import contracts
import fan
import format as formatter
import namespaces
import pointers
import reserved
import shape
import spellings

MANY   = range(fan.SERIAL_BELOW * 4)
FOLDER = 'sources/sdl-rdp/video'
SEEDED = '''#include <sdl-rdp/utilities/contract.hpp>

namespace Seeded{n} {{
auto Mutate(int& value) -> bool;
auto Checked{n}(int& value) -> int {{
  Expects(Mutate(value) && value > {n}, "mutated");
  unsigned counter = {n};
  int _Reserved{n} = (int)counter;
  int a__b = 1;
  int longer_name = 2;
  if(value){{return a__b+_Reserved{n}+longer_name;}}
  return value;
}}
int Leading{n}(int a) {{ return a; }}
struct Flag{n} {{
  operator bool() {{ return true; }}
}};
auto Long{n}(int value) -> int {{
{body}  return value;
}}
}}
'''


class Recording(concurrent.futures.ProcessPoolExecutor):
    """A process pool that counts itself, so a test knows the pool branch ran."""
    opened = 0

    def __init__(self, *arguments, **options):
        type(self).opened += 1
        super().__init__(*arguments, **options)


@pytest.fixture
def pooled(monkeypatch):
    """Every fan goes through a real pool of four workers, however few the items or the cores."""
    monkeypatch.setattr(fan, 'SERIAL_BELOW', 0)
    monkeypatch.setattr(fan, 'worker_count', lambda: 4)
    monkeypatch.setattr(concurrent.futures, 'ProcessPoolExecutor', Recording)
    Recording.opened = 0
    return Recording


def test_results_follow_the_items_order_across_workers(pooled):
    assert fan.out(operator.mul, MANY, 3) == [3 * item for item in MANY]
    assert pooled.opened == 1


def test_two_pools_in_succession_each_hand_their_own_shared_arguments(pooled):
    assert fan.out(operator.mul, MANY, 3) == [3 * item for item in MANY]
    assert fan.out(operator.mul, MANY, 5) == [5 * item for item in MANY]
    assert pooled.opened == 2


def test_a_pool_opened_from_a_thread_while_another_thread_runs(pooled):
    release = threading.Event()
    waiting = threading.Thread(target=release.wait)
    waiting.start()
    try:
        with concurrent.futures.ThreadPoolExecutor(1) as caller:
            found = caller.submit(fan.out, operator.mul, MANY, 7).result()
    finally:
        release.set()
        waiting.join()
    assert found == [7 * item for item in MANY]
    assert fan.CONTEXT.get_start_method() != 'fork'


def test_flattened_concatenates_each_items_results_in_order(pooled):
    assert fan.flattened(range, MANY, 0) == [value for item in MANY for value in range(item)]


def test_a_workers_failure_reaches_the_caller_naming_its_item(pooled):
    with pytest.raises(fan.ItemFailed, match='^0: ZeroDivisionError: '):
        fan.out(operator.truediv, MANY, 1)


def test_an_in_process_failure_names_its_item_over_the_original():
    with pytest.raises(fan.ItemFailed, match='^0: ZeroDivisionError: ') as failed:
        fan.out(operator.truediv, [0], 1)
    assert isinstance(failed.value.__cause__, ZeroDivisionError)


@pytest.fixture
def undecodable(tmp_path):
    """A tree past SERIAL_BELOW files, one of them not UTF-8."""
    (tmp_path / FOLDER).mkdir(parents=True)
    for n in range(fan.SERIAL_BELOW + 8):
        (tmp_path / FOLDER / f'good-{n}.cpp').write_text('auto F() -> int;\n')
    (tmp_path / FOLDER / 'latin.cpp').write_bytes(b'auto Caf\xe9() -> int;\n')
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    subprocess.run(['git', 'add', '-A'], cwd=tmp_path, check=True)
    return tmp_path


@pytest.mark.parametrize('pool', [True, False], ids=['pooled', 'in-process'])
def test_a_file_that_is_not_utf8_is_named_by_the_fanned_lint(undecodable, monkeypatch, pooled, pool):
    monkeypatch.setattr(fan, 'SERIAL_BELOW', 0 if pool else math.inf)
    monkeypatch.chdir(undecodable)
    with pytest.raises(fan.ItemFailed, match=f'^{FOLDER}/latin.cpp: UnicodeDecodeError'):
        columns.main(['--check', 'sources'])


def test_a_file_that_is_not_utf8_is_named_by_contracts(undecodable):
    with pytest.raises(fan.ItemFailed, match=f'^{FOLDER}/latin.cpp: UnicodeDecodeError'):
        contracts.findings(undecodable)


def test_a_few_items_run_without_a_pool(monkeypatch):
    monkeypatch.setattr(concurrent.futures, 'ProcessPoolExecutor', None)
    assert fan.out(operator.neg, [4, 5]) == [-4, -5]
    assert fan.out(operator.neg, []) == []


@pytest.mark.parametrize(('text', 'cores'), [('max 100000\n', None), ('250000 100000\n', 3), ('200000 100000\n', 2),
                                             ('garbled\n', None)])
def test_the_cgroup_quota_is_whole_cores_rounded_up(tmp_path, text, cores):
    (tmp_path / 'cpu.max').write_text(text)
    assert fan.quota(tmp_path / 'cpu.max') == cores


def test_no_quota_file_is_no_quota(tmp_path):
    assert fan.quota(tmp_path / 'cpu.max') is None


def test_an_unreadable_quota_is_no_quota(tmp_path):
    assert fan.quota(tmp_path) is None


def test_workers_are_the_fewer_of_the_affinity_and_the_quota(tmp_path, monkeypatch):
    monkeypatch.setattr(fan.os, 'process_cpu_count', lambda: 88)
    (tmp_path / 'cpu.max').write_text('250000 100000\n')
    assert fan.worker_count(tmp_path / 'cpu.max') == 3
    (tmp_path / 'cpu.max').write_text('max 100000\n')
    assert fan.worker_count(tmp_path / 'cpu.max') == 88


@pytest.fixture(scope='module')
def seeded(tmp_path_factory):
    """A tree past SERIAL_BELOW files, each holding a violation for every fanned lint."""
    root = tmp_path_factory.mktemp('seeded')
    (root / FOLDER).mkdir(parents=True)
    body = ''.join(f'  value += {step};\n' for step in range(45))
    for n in range(fan.SERIAL_BELOW + 8):
        (root / FOLDER / f'seeded-{n}.cpp').write_text(SEEDED.format(n=n, body=body))
    (root / FOLDER / 'check.hpp').write_text('Expects(Mutate(), "changed");\n')
    (root / 'shape.allow').write_text('')
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', 'add', '-A'], cwd=root, check=True)
    return root


def database_units(root):
    database = root / 'compile_commands.json'
    database.write_text(json.dumps([{'directory': str(root), 'file': str(path),
                                     'command': f'clang++ -std=c++26 -I"{root}/include" -c "{path}" -o {path.stem}.o'}
                                    for path in sorted((root / FOLDER).glob('*.cpp'))]))
    return pointers.database_units(root, database, {})


LINTS = {'shape':      lambda root: shape.main(['--allow', str(root / 'shape.allow')]),
         'columns':    lambda root: columns.main(['--check', 'sources']),
         'format':     lambda root: formatter.main(['--check', str(root / 'sources')]),
         'spellings':  spellings.findings,
         'reserved':   reserved.findings,
         'casts':      casts.findings,
         'namespaces': namespaces.findings,
         'contracts':  contracts.findings,
         'pointers':   database_units}


def linted(root, lint, monkeypatch):
    monkeypatch.chdir(root)
    printed = io.StringIO()
    with contextlib.redirect_stdout(printed):
        result = LINTS[lint](root)
    return result, re.sub(r'\S*?/sources/', 'sources/', printed.getvalue())


@pytest.mark.parametrize('lint', LINTS)
def test_a_fanned_lint_finds_what_it_finds_in_process(seeded, lint, monkeypatch, pooled):
    fanned = linted(seeded, lint, monkeypatch)
    opened = pooled.opened
    assert opened
    monkeypatch.setattr(fan, 'SERIAL_BELOW', math.inf)
    assert linted(seeded, lint, monkeypatch) == fanned
    assert pooled.opened == opened
    assert fanned[0] or fanned[1]
