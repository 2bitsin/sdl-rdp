"""The clone lint's background run: its streams kept apart and its whole process tree ended with it."""
import io
import os
import pathlib
import sys
import time

import clones
import test_gate

SPAWNING = ('import subprocess, sys, time\n'
            'child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
            'print(child.pid, flush=True)\n')
LINGERING = SPAWNING + 'time.sleep(60)\n'


def alive(pid):
    """Whether the pid is running: neither gone nor a zombie."""
    try:
        return pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0] != 'Z'
    except FileNotFoundError:
        return False


def printed_pid(run):
    """The pid the stand-in prints, read once it has printed it."""
    output = pathlib.Path(f'/proc/self/fd/{run.output.fileno()}')
    while not (text := output.read_text()) and run.process.poll() is None:
        time.sleep(0.01)
    return int(text)


def test_a_failed_runs_diagnostic_stays_on_stderr(capsys):
    failing = [sys.executable, '-c', 'import sys; print("scanned"); sys.exit("npm ERR! download failed")']
    with clones.running(failing) as run:
        assert clones.finished(run) == 1
    captured = capsys.readouterr()
    assert (captured.out, captured.err) == ('scanned\n', 'npm ERR! download failed\n')


def ended_in_time(pid):
    """Whether the pid is gone within ten seconds."""
    deadline = time.monotonic() + 10
    while alive(pid) and time.monotonic() < deadline:
        time.sleep(0.01)
    return not alive(pid)


def test_leaving_the_block_early_ends_the_child_the_wrapper_spawned():
    try:
        with clones.running([sys.executable, '-c', LINGERING]) as run:
            child = printed_pid(run)
            raise KeyboardInterrupt
    except KeyboardInterrupt:
        pass
    assert ended_in_time(child)


def test_a_child_outliving_its_exited_wrapper_ends_with_the_block():
    with clones.running([sys.executable, '-c', SPAWNING]) as run:
        os.waitid(os.P_PID, run.process.pid, os.WEXITED | os.WNOWAIT)
        child = printed_pid(run)
        assert alive(child)
        assert clones.finished(run) == 0
    assert ended_in_time(child)


class Recorded:
    """A process double whose waits land in the shared call log."""
    pid = 12345

    def __init__(self, calls):
        self.calls      = calls
        self.returncode = None

    def wait(self):
        self.calls.append('wait')
        self.returncode = 0
        return 0


def test_the_group_is_signalled_once_and_only_before_its_leader_is_reaped(monkeypatch):
    calls = []
    monkeypatch.setattr(clones.os, 'waitid', lambda *arguments: calls.append('waitid'))
    monkeypatch.setattr(clones.os, 'killpg', lambda *arguments: calls.append('killpg'))
    process = Recorded(calls)
    assert clones.finished(clones.Run(process, io.BytesIO(), io.BytesIO())) == 0
    clones.ended(process)
    assert calls == ['waitid', 'killpg', 'wait']


def test_jscpd_starts_early_only_when_the_session_runs_test_clones():
    assert test_gate.overlapped({'test_shape', 'test_clones'}, None)
    assert not test_gate.overlapped({'test_shape'}, None)


def test_an_xdist_worker_runs_jscpd_in_test_clones_itself():
    assert not test_gate.overlapped({'test_clones'}, 'gw0')
