"""The facade lint: FreeRDP includes and names per file outside the facade, held to a shrink-only baseline."""
import collections
import pathlib
import subprocess

import pytest

import facade

NAMES = frozenset(('rdpContext', 'UINT32', 'CHANNEL_RC_OK'))


@pytest.fixture
def tree(tmp_path):
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    (tmp_path / 'sources/sdl-rdp/freerdp-facade').mkdir(parents=True)
    (tmp_path / 'sources/sdl-rdp/video').mkdir(parents=True)
    return tmp_path


def counted(tree, files):
    for name, text in files.items():
        (tree / name).write_text(text)
    subprocess.run(['git', 'add', '-A'], cwd=tree, check=True)
    return {section: dict(counter) for section, counter in facade.counts(tree, NAMES).items()}


def baseline(**sections):
    held = {section: collections.Counter() for section in facade.SECTIONS}
    held.update({section.replace('_', ' '): collections.Counter(entries) for section, entries in sections.items()})
    return held


def test_includes_outside_the_facade_are_counted(tree):
    text = '#include <freerdp/peer.h>\n#  include <winpr/wtypes.h>\n#include <oxbox/utilities/text.hpp>\n'
    assert counted(tree, {'sources/sdl-rdp/video/a.cpp': text})['includes'] == {'sources/sdl-rdp/video/a.cpp': 2}


def test_the_facade_sources_include_freely_and_its_headers_are_counted(tree):
    found = counted(tree, {'sources/sdl-rdp/freerdp-facade/a.cpp': '#include <freerdp/peer.h>\nrdpContext* c;\n',
                           'sources/sdl-rdp/freerdp-facade/a.hpp': '#include <winpr/wtypes.h>\n'})
    assert found == {'includes': {}, 'names': {}, 'facade headers': {'sources/sdl-rdp/freerdp-facade/a.hpp': 1}}


def test_names_are_counted_in_code_and_directives(tree):
    text = 'auto F(rdpContext& c) -> UINT32;\n#define OK CHANNEL_RC_OK\nauto g = CHANNEL_RC_OK;\n'
    assert counted(tree, {'sources/sdl-rdp/video/a.hpp': text})['names'] == {'sources/sdl-rdp/video/a.hpp': 4}


def test_comments_literals_and_disabled_code_are_not_counted(tree):
    text = '// abi: UINT32 is uint32_t\nauto a = "rdpContext";\n/* CHANNEL_RC_OK */\n#if 0\nUINT32 b;\n#endif\n'
    assert counted(tree, {'sources/sdl-rdp/video/a.cpp': text})['names'] == {}


@pytest.mark.parametrize('path', [
    'sources/sdl-rdp/headless-client.test/client/channels.cpp', 'sources/sdl-rdp/integration/audio.test/rate.cpp',
    'sources/sdl-rdp/integration/support.bench/adapter.hpp', 'sources/sdl-rdp/integration/main.cpp',
    'sources/sdl-rdp/video/encoder.test.cpp', 'sources/sdl-rdp/video/support.test.hpp',
    'sources/sdl-rdp/video/encoder.bench.cpp', 'sources/sdl-rdp/utilities/unit.test/support.hpp'])
def test_tests_benches_rigs_and_integration_do_not_ship(path):
    assert not facade.production(pathlib.Path(path))


@pytest.mark.parametrize('path', [
    'sources/sdl-rdp/video/encoder.cpp', 'sources/sdl-rdp/SDL3/rdp/driver.hpp', 'sources/sample/main.cpp',
    'sources/sample/main.test.cpp'])
def test_the_package_and_the_whole_sample_ship(path):
    assert facade.production(pathlib.Path(path))


def test_a_test_includes_freely(tree):
    (tree / 'sources/sdl-rdp/integration').mkdir()
    found = counted(tree, {'sources/sdl-rdp/video/a.test.cpp': '#include <freerdp/peer.h>\nrdpContext* c;\n',
                           'sources/sdl-rdp/integration/a.cpp': '#include <freerdp/peer.h>\nUINT32 a;\n'})
    assert found == {'includes': {}, 'names': {}, 'facade headers': {}}


def test_growth_and_a_new_file_fail():
    found = baseline(names={'a.cpp': 3, 'b.cpp': 1})
    assert facade.regressions(found, baseline(names={'a.cpp': 2})) == [
        'a.cpp: names 3, baseline 2',
        "b.cpp: names 1, not in the baseline; a file moved from another path takes that path's line in facade.baseline,"
        ' renamed']


def test_a_fall_fails_until_the_baseline_records_it():
    assert facade.regressions(baseline(includes={'a.cpp': 1}), baseline(includes={'a.cpp': 2, 'b.cpp': 1})) == [
        'a.cpp: includes fell to 1 from 2; run facade.py --update',
        'b.cpp: includes fell to 0 from 1; run facade.py --update']


def test_an_equal_count_passes():
    assert facade.regressions(baseline(facade_headers={'a.hpp': 1}), baseline(facade_headers={'a.hpp': 1})) == []


def test_update_records_falls_drops_zeros_and_never_grows_or_adds():
    found = baseline(names={'a.cpp': 1, 'c.cpp': 9, 'd.cpp': 5})
    held  = baseline(names={'a.cpp': 2, 'b.cpp': 4, 'd.cpp': 3})
    assert facade.shrunk(found, held) == baseline(names={'a.cpp': 1, 'd.cpp': 3})


def test_the_baseline_round_trips(tmp_path):
    held = baseline(includes={'a.cpp': 2}, names={'a.cpp': 7, 'b.hpp': 1}, facade_headers={'f.hpp': 1})
    path = tmp_path / 'baseline'
    path.write_text(facade.rendered(held))
    assert facade.read_baseline(path) == held


@pytest.mark.parametrize('line', ['a.cpp: 1', '[names]\na.cpp 1', '[names]\na.cpp: 0', '[other]\na.cpp: 1'])
def test_the_baseline_refuses_any_other_line(tmp_path, line):
    path = tmp_path / 'baseline'
    path.write_text(line + '\n')
    with pytest.raises(SystemExit, match='not a section'):
        facade.read_baseline(path)


@pytest.fixture
def held(tree, monkeypatch):
    (tree / 'names').write_text('FreeRDP 3.32.0\nUINT32\ttypedef\twinpr/wtypes.h\n')
    counted(tree, {'sources/sdl-rdp/video/a.cpp': 'UINT32 a;\nUINT32 b;\n'})
    monkeypatch.setattr(facade, 'ROOT', tree)
    return ['--baseline', str(tree / 'baseline'), '--names', str(tree / 'names')]


def test_an_absent_baseline_fails_and_is_never_reseeded_by_update(tree, held):
    for arguments in (held, [*held, '--update']):
        with pytest.raises(SystemExit, match='no baseline'):
            facade.main(arguments)
    assert not (tree / 'baseline').exists()


def test_seed_writes_once_and_refuses_an_existing_baseline(tree, held, capsys):
    assert facade.main([*held, '--seed']) == 0
    seeded = '[includes]\n\n[names]\nsources/sdl-rdp/video/a.cpp: 2\n\n[facade headers]\n'
    assert (tree / 'baseline').read_text() == seeded
    assert 'names: 0 files, 0 -> 1 files, 2' in capsys.readouterr().out
    with pytest.raises(SystemExit, match='exists'):
        facade.main([*held, '--seed'])


def test_update_shrinks_an_existing_baseline_and_prints_the_totals(tree, held, capsys):
    (tree / 'baseline').write_text('[names]\nsources/sdl-rdp/video/a.cpp: 5\nsources/sdl-rdp/video/b.cpp: 1\n')
    assert facade.main(held) == 1
    capsys.readouterr()
    assert facade.main([*held, '--update']) == 0
    assert capsys.readouterr().out.splitlines() == [
        'includes: 0 files, 0 -> 0 files, 0', 'names: 2 files, 6 -> 1 files, 2',
        'facade headers: 0 files, 0 -> 0 files, 0']
    assert facade.main(held) == 0
    counted(tree, {'sources/sdl-rdp/video/a.cpp': 'UINT32 a;\nUINT32 b;\nUINT32 c;\n'})
    assert facade.main(held) == 1
