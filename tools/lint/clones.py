#!/usr/bin/env python3
"""Fail on duplicated C and C++ code under sources/, headers included."""
import contextlib
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import typing

ROOT     = pathlib.Path(__file__).resolve().parents[2]
JSCPD    = 'jscpd@4.0.5'
INCLUDES = r'#include\s+[<"][^>"]*[>"]'
IMPORTS  = r'using\s+[\w:]+(?:operator""\w+)?\s*;'


def command():
    return ['npx', '--yes', JSCPD, '--min-tokens', '40', '--min-lines', '5', '--format', 'cpp',
            '--formats-exts', 'cpp:cpp,hpp,c,h', '--ignore-pattern', f'{INCLUDES},{IMPORTS}', '--noSymlinks',
            '--exitCode', '1', 'sources']


class Run(typing.NamedTuple):
    process: subprocess.Popen
    output:  typing.IO[bytes]
    errors:  typing.IO[bytes]


def ended(process):
    """Everything left in the process's session killed, then the process reaped; nothing once it is reaped."""
    if process.returncode is not None:
        return
    # The group id is reserved only while its leader is unreaped (waitid(2)), so the signal comes before the wait.
    with contextlib.suppress(ProcessLookupError):
        os.killpg(process.pid, signal.SIGKILL)
    process.wait()


@contextlib.contextmanager
def running(arguments=None):
    """jscpd over sources/ in the background for the block, its streams kept for `finished`; none without npx."""
    if arguments is None and shutil.which('npx') is None:
        yield None
        return
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(command() if arguments is None else arguments, cwd=ROOT, stdout=output,
                                   stderr=errors, start_new_session=True)
        try:
            yield Run(process, output, errors)
        finally:
            ended(process)


def replayed(recorded, stream):
    recorded.seek(0)
    print(recorded.read().decode(errors='replace'), end='', file=stream)


def finished(run):
    """The run's exit code, its stdout and stderr replayed to the streams they came from."""
    if run is None:
        print('clones: npx is not installed', file=sys.stderr)
        return 1
    os.waitid(os.P_PID, run.process.pid, os.WEXITED | os.WNOWAIT)
    ended(run.process)
    replayed(run.output, sys.stdout)
    replayed(run.errors, sys.stderr)
    return run.process.returncode


def main():
    with running() as run:
        return finished(run)


if __name__ == '__main__':
    sys.exit(main())
