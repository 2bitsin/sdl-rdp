"""Code, comment and blank lines of the tracked tree through cloc, by language, component and file."""
import argparse
import collections
import html
import json
import os
import shutil
import subprocess
import sys

SECTIONS = ('language', 'component', 'file')


def tracked(root):
    return subprocess.run(['git', '-C', root, 'ls-files', '-z'], capture_output=True, text=True, check=True).stdout


def counted(root):
    if not shutil.which('cloc'):
        raise RuntimeError('cloc is not installed (the build image adds it)')
    result = subprocess.run(['cloc', '--list-file=-', '--by-file', '--json', '--quiet'], cwd=root,
                            input=tracked(root).replace('\0', '\n'), capture_output=True, text=True, check=True)
    table = json.loads(result.stdout or '{}')
    return {path: entry for path, entry in table.items() if path not in ('header', 'SUM')}


def component(path):
    parts = path.split('/')
    if parts[0] == 'sources' and parts[1] == 'sdl-rdp':
        return 'SDL3 driver' if parts[2] == 'SDL3' else parts[2] if len(parts) > 3 else 'sources (root)'
    if parts[0] == 'sources':
        return parts[1] if len(parts) > 2 else 'sources (root)'
    if parts[0] == 'tools':
        return f'tools/{parts[1]}' if len(parts) > 2 else 'tools'
    return parts[0] if len(parts) > 1 else '(root)'


def grouped(files, key):
    totals = collections.defaultdict(lambda: [0, 0, 0, 0])
    for path, entry in files.items():
        total = totals[key(path, entry)]
        total[0] += 1
        total[1] += entry['code']
        total[2] += entry['comment']
        total[3] += entry['blank']
    return sorted(totals.items(), key=lambda item: -item[1][1])


def print_group(title, groups, all_code, out):
    print(f'== {title} ==', file=out)
    print(f"{'':30} {'files':>5} {'code':>7} {'comment':>8} {'blank':>6} {'share':>6}", file=out)
    for name, (files, code, comment, blank) in groups:
        print(f'{name:30} {files:5} {code:7} {comment:8} {blank:6} {100 * code / all_code:5.1f}%', file=out)
    print(file=out)


def print_files(files, top, out):
    print('== By file (code, comment, blank) ==', file=out)
    for path, entry in sorted(files.items(), key=lambda item: -item[1]['code'])[:top]:
        print(f"{entry['code']:6} {entry['comment']:5} {entry['blank']:5}  {path}", file=out)


def revision(root):
    command = ['git', '-C', root, 'rev-parse', '--short', 'HEAD']
    return subprocess.run(command, capture_output=True, text=True).stdout.strip()


HTML_HEAD = """<!doctype html><meta charset="utf-8"><title>{title}</title>
<style>
body{{font:14px/1.4 system-ui,sans-serif;margin:2em;color:#222}} h2{{margin-top:2em}}
table{{border-collapse:collapse}} th,td{{padding:2px 10px;text-align:right;white-space:nowrap}}
th{{cursor:pointer;background:#eee;position:sticky;top:0}} td:first-child,th:first-child{{text-align:left}}
tr:nth-child(even){{background:#f7f7f7}} input{{margin:0 0 .5em;width:30em}}
</style>
<script>
function sortBy(th){{
  const t=th.closest('table'),i=th.cellIndex,asc=th.dataset.asc!=='1';th.dataset.asc=asc?'1':'';
  const v=r=>{{const s=r.cells[i].textContent.replace('%','');return isNaN(s)?s:+s}};
  const rows=[...t.tBodies[0].rows];
  rows.sort((a,b)=>{{const x=v(a),y=v(b);return (x<y?-1:x>y?1:0)*(asc?1:-1)}});
  rows.forEach(r=>t.tBodies[0].appendChild(r))}}
function filter(input){{
  const q=input.value.toLowerCase();
  for(const r of input.nextElementSibling.tBodies[0].rows)
    r.style.display=r.textContent.toLowerCase().includes(q)?'':'none'}}
</script>
"""


FILE_COLUMNS = ('file', 'component', 'language', 'code', 'comment', 'blank')


def html_table(columns, rows, filterable=False):
    head = ''.join(f'<th onclick="sortBy(this)">{html.escape(c)}</th>' for c in columns)
    cell = lambda value: f'<td>{html.escape(str(value))}</td>'
    body = ''.join('<tr>' + ''.join(cell(v) for v in row) + '</tr>' for row in rows)
    box  = '<input placeholder="filter" oninput="filter(this)">' if filterable else ''
    return f'{box}<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>'


def html_report(root, files):
    all_code = sum(entry['code'] for entry in files.values())
    title    = f'{os.path.basename(os.path.abspath(root))} {revision(root)}'
    share    = lambda code: f'{100 * code / all_code:.1f}%'
    group    = lambda groups: [(n, f, c, m, b, share(c)) for n, (f, c, m, b) in groups]
    columns  = ('name', 'files', 'code', 'comment', 'blank', 'share')
    by_file  = [(path, component(path), entry['language'], entry['code'], entry['comment'], entry['blank'])
                for path, entry in sorted(files.items(), key=lambda item: -item[1]['code'])]
    return (HTML_HEAD.format(title=html.escape(title)) + f'<h1>{html.escape(title)}</h1>'
            f"<p>{len(files)} files, {all_code} code, {sum(e['comment'] for e in files.values())} comment, "
            f"{sum(e['blank'] for e in files.values())} blank lines. Click a header to sort.</p>"
            '<h2>By component</h2>' + html_table(columns, group(grouped(files, lambda p, e: component(p))))
            + '<h2>By language</h2>' + html_table(columns, group(grouped(files, lambda p, e: e['language'])))
            + '<h2>By file</h2>' + html_table(FILE_COLUMNS, by_file, True))


def report(root, sections, top, out=sys.stdout, html_path=None):
    files    = counted(root)
    all_code = sum(entry['code'] for entry in files.values())
    if html_path:
        with open(html_path, 'w') as page:
            page.write(html_report(root, files))
    print(f'{os.path.basename(os.path.abspath(root))} {revision(root)}: {len(files)} files, {all_code} code, '
          f"{sum(entry['comment'] for entry in files.values())} comment, "
          f"{sum(entry['blank'] for entry in files.values())} blank lines\n", file=out)
    if 'language' in sections:
        print_group('By language', grouped(files, lambda path, entry: entry['language']), all_code, out)
    if 'component' in sections:
        print_group('By component', grouped(files, lambda path, entry: component(path)), all_code, out)
    if 'file' in sections:
        print_files(files, top, out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    parser.add_argument('--by', action='append', choices=SECTIONS, help='a section to print; default all three')
    parser.add_argument('--top', type=int, default=None, help='largest files to list; default all')
    parser.add_argument('--html', default=None, help='also write the report as one sortable html page here')
    args = parser.parse_args(argv)
    report(args.root, args.by or SECTIONS, args.top, html_path=args.html)
    return 0


if __name__ == '__main__':
    sys.exit(main())
