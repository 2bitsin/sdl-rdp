import io
import shutil
import subprocess

import pytest

import lines


def test_component_classifies_the_known_shapes():
    assert lines.component('sources/sdl-rdp/SDL3/rdp/driver.cpp') == 'SDL3 driver'
    assert lines.component('sources/sdl-rdp/peer/loop.cpp') == 'peer'
    assert lines.component('sources/sample/main.cpp') == 'sample'
    assert lines.component('tools/lint/shape.py') == 'tools/lint'
    assert lines.component('README.md') == '(root)'


@pytest.mark.skipif(not shutil.which('cloc'), reason='cloc is not installed')
def test_report_counts_code_without_comments_and_blanks(tmp_path):
    subprocess.run(['git', 'init', '-q', tmp_path], check=True)
    (tmp_path / 'a.cpp').write_text('// why\nint x;\n\nint y;\n')
    (tmp_path / 'b.py').write_text('# why\nx = 1\n')
    subprocess.run(['git', '-C', tmp_path, 'add', '.'], check=True)
    out = io.StringIO()
    lines.report(str(tmp_path), lines.SECTIONS, None, out)
    text = out.getvalue()
    assert '2 files, 3 code, 2 comment, 1 blank lines' in text
    assert text.index('C++') < text.index('Python')
    assert text.strip().endswith('1     1     0  b.py')


@pytest.mark.skipif(not shutil.which('cloc'), reason='cloc is not installed')
def test_html_report_lists_components_and_files_sorted_by_code(tmp_path):
    subprocess.run(['git', 'init', '-q', tmp_path], check=True)
    (tmp_path / 'sources').mkdir()
    (tmp_path / 'sources' / 'big.cpp').write_text('int a;\nint b;\nint c;\n')
    (tmp_path / 'small.py').write_text('x = 1\n')
    subprocess.run(['git', '-C', tmp_path, 'add', '.'], check=True)
    page = tmp_path / 'lines.html'
    lines.report(str(tmp_path), (), None, io.StringIO(), html_path=str(page))
    text = page.read_text()
    assert '2 files, 4 code' in text
    assert text.index('sources/big.cpp') < text.index('small.py')
    assert '<td>sources (root)</td><td>1</td><td>3</td>' in text
