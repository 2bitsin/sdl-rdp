#!/usr/bin/env python3
"""Measure the per-present cost of the driver and the backend compose under callgrind."""
import argparse
import collections
import concurrent.futures
import contextlib
import dataclasses
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT      = pathlib.Path(__file__).resolve().parents[2]
SIZE      = '1280x800'
SCENARIOS = ('planar:partial', 'planar:full', 'progressive:full')
TOOLS     = ('valgrind', 'callgrind_control', 'Xvfb', 'xfreerdp3', 'ss')
# Under valgrind the sample takes minutes to reach its first present on a loaded box.
CONNECT_SECONDS = 240


@dataclasses.dataclass(frozen=True)
class Scenario:
    codec:   str
    damage:  str
    port:    int
    display: int

    @property
    def label(self) -> str:
        return f'{self.codec}-{self.damage}'


@dataclasses.dataclass
class Profile:
    self_cost: collections.Counter = dataclasses.field(default_factory=collections.Counter)
    calls:     collections.Counter = dataclasses.field(default_factory=collections.Counter)
    inclusive: collections.Counter = dataclasses.field(default_factory=collections.Counter)


class ProfileReader:
    """Callgrind's line format: name-compressed positions, then cost lines owned by a function or a call."""

    def __init__(self) -> None:
        self.profile  = Profile()
        self.names    = {}
        self.function = ''
        self.callee   = ''
        self.pending  = None
        self.handlers = {'fn': self.enter, 'cfn': self.call_target, 'calls': self.call_count}

    def resolve(self, text: str) -> str:
        match = re.match(r'\((\d+)\)(?: (.*))?', text)
        if not match:
            return text
        if match.group(2) is not None:
            self.names[match.group(1)] = match.group(2)
        return self.names[match.group(1)]

    def enter(self, value: str) -> None:
        self.function = self.resolve(value)

    def call_target(self, value: str) -> None:
        self.callee = self.resolve(value)

    def call_count(self, value: str) -> None:
        self.pending = int(value.split()[0])

    def cost(self, line: str) -> None:
        fields = line.split()
        cost = int(fields[1]) if len(fields) > 1 else 0
        if self.pending is None:
            self.profile.self_cost[self.function] += cost
            return
        self.profile.calls[(self.function, self.callee)] += self.pending
        self.profile.inclusive[(self.function, self.callee)] += cost
        self.pending = None

    def read(self, line: str) -> None:
        key, separator, value = line.partition('=')
        if separator and key in self.handlers:
            self.handlers[key](value)
        elif line[:1].isdigit() or line[:1] in '+-*':
            self.cost(line)


def parse_profile(path: pathlib.Path) -> Profile:
    """Self cost per function, and call count and inclusive cost per caller-callee edge."""
    reader = ProfileReader()
    for line in path.read_text(errors='replace').splitlines():
        reader.read(line)
    return reader.profile


def is_allocator(name: str) -> bool:
    return name.startswith('operator new') or name in ('malloc', 'calloc', 'realloc')


def is_driver(name: str) -> bool:
    return name.startswith(('rdp::', 'void rdp::', 'SDL_VideoData::', 'SDL_PrivateAudioData::'))


def is_compose(name: str) -> bool:
    return name.startswith(('Backend::Presenter::Present(', 'Backend::FrameSnapshot::'))


def summarize(profile: Profile) -> dict:
    """The three per-present numbers: memset from the event poll, compose, driver allocations."""
    presents = sum(count for (_, callee), count in profile.calls.items()
                   if callee.startswith('Backend::Presenter::Present('))
    per = max(presents, 1)
    poll_memset = sum(cost for (caller, callee), cost in profile.inclusive.items()
                      if 'memset' in callee and 'rdp::Driver::Poll' in caller)
    compose = sum(cost for name, cost in profile.self_cost.items() if is_compose(name))
    allocations = sum(count for (caller, callee), count in profile.calls.items()
                      if is_allocator(callee) and is_driver(caller))
    return {'presents': presents, 'poll memset Ir/present': poll_memset / per,
            'compose Ir/present': compose / per, 'driver allocations/present': allocations / per}


def wait_for(condition, seconds: float) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.5)
    return False


@contextlib.contextmanager
def running(command: list, **options):
    """A child process that is terminated and reaped however the block exits, Ctrl-C included."""
    process = subprocess.Popen(command, **options)
    try:
        yield process
    finally:
        process.terminate()
        process.wait()


def sample_environment(install: pathlib.Path, scenario: Scenario, certificates: str) -> dict:
    return {**os.environ, 'LD_LIBRARY_PATH': str(install), 'SDL_VIDEO_DRIVER': 'rdp',
            'SDL_RDP_PORT': str(scenario.port), 'SDL_RDP_USER': 'qa', 'SDL_RDP_PASSWORD': 'qa',
            'SDL_RDP_CODEC': scenario.codec, 'SDL_RDP_CERT_DIR': certificates}


def sample_command(install: pathlib.Path, scenario: Scenario, result: pathlib.Path) -> list:
    flags = ['--tight', '--size', SIZE] + (['--partial'] if scenario.damage == 'partial' else [])
    return ['valgrind', '--tool=callgrind', '--instr-atstart=no', f'--callgrind-out-file={result}',
            str(install / 'sdl-rdp-sample'), *flags]


def client_command(scenario: Scenario) -> list:
    return ['xfreerdp3', f'/v:127.0.0.1:{scenario.port}', f'/size:{SIZE}', '/cert:ignore', '/sec:tls', '/u:qa',
            '/p:qa', '/log-level:ERROR', '-sound', '-clipboard']


def listening(port: int) -> bool:
    return f':{port} ' in subprocess.run(['ss', '-ltn'], capture_output=True, text=True).stdout


def instrument(sample: subprocess.Popen, seconds: int) -> None:
    subprocess.run(['callgrind_control', '-i', 'on', str(sample.pid)], capture_output=True, check=True)
    time.sleep(seconds)
    subprocess.run(['callgrind_control', '-i', 'off', str(sample.pid)], capture_output=True, check=True)


def attach_client(scenario: Scenario, log: pathlib.Path, out: pathlib.Path, sample, seconds: int) -> bool:
    """Connect the client, then instrument only while it has the window focused."""
    with open(out / f'{scenario.label}.client.log', 'w') as client_log, \
            running(client_command(scenario), env={**os.environ, 'DISPLAY': f':{scenario.display}'},
                    stdout=client_log, stderr=subprocess.STDOUT):
        if not wait_for(lambda: 'FOCUS_GAINED' in log.read_text(errors='replace'), CONNECT_SECONDS):
            return False
        instrument(sample, seconds)
        return True


def profile_scenario(install: pathlib.Path, out: pathlib.Path, scenario: Scenario, seconds: int) -> pathlib.Path:
    """Display, sample under callgrind, client, instrumentation window; every child is reaped on the way out."""
    result = out / f'{scenario.label}.callgrind'
    log = out / f'{scenario.label}.sample.log'
    with tempfile.TemporaryDirectory() as certificates, open(log, 'w') as sample_log, \
            running(['Xvfb', f':{scenario.display}', '-screen', '0', '1600x1000x24'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL), \
            running(sample_command(install, scenario, result), stdout=sample_log, stderr=subprocess.STDOUT,
                    env=sample_environment(install, scenario, certificates)) as sample:
        if not wait_for(lambda: listening(scenario.port), CONNECT_SECONDS):
            sys.exit(f'{scenario.label}: the sample never listened on {scenario.port}, see {log}')
        if not attach_client(scenario, log, out, sample, seconds):
            sys.exit(f'{scenario.label}: the client never focused the window, see {log}')
    return result


def scenarios(names: list, port: int, display: int) -> list:
    return [Scenario(*name.split(':'), port + index, display + index) for index, name in enumerate(names)]


def print_summary(label: str, summary: dict) -> None:
    print(f'{label}: ' + ', '.join(f'{key} {value:,.3f}' if isinstance(value, float) else f'{key} {value}'
                                   for key, value in summary.items()))


def run(arguments: argparse.Namespace) -> None:
    if missing := [tool for tool in TOOLS if not shutil.which(tool)]:
        sys.exit(f'missing tools: {", ".join(missing)}')
    arguments.out.mkdir(parents=True, exist_ok=True)
    chosen = scenarios(arguments.scenario, arguments.port, arguments.display)
    with concurrent.futures.ThreadPoolExecutor(len(chosen)) as pool:
        results = list(pool.map(lambda scenario: profile_scenario(arguments.install, arguments.out, scenario,
                                                                  arguments.seconds), chosen))
    for scenario, result in zip(chosen, results):
        print_summary(scenario.label, summarize(parse_profile(result)))


def analyze(arguments: argparse.Namespace) -> None:
    for path in arguments.profiles:
        print_summary(path.name, summarize(parse_profile(path)))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(required=True)
    runner = commands.add_parser('run', help='profile the release sample with a client attached')
    runner.add_argument('--install', type=pathlib.Path, default=ROOT / '_install')
    runner.add_argument('--out', type=pathlib.Path, required=True)
    runner.add_argument('--seconds', type=int, default=60)
    runner.add_argument('--port', type=int, default=43392)
    runner.add_argument('--display', type=int, default=96)
    runner.add_argument('--scenario', nargs='+', default=list(SCENARIOS), help='codec:full or codec:partial')
    runner.set_defaults(command=run)
    reader = commands.add_parser('analyze', help='summarize existing callgrind output files')
    reader.add_argument('profiles', type=pathlib.Path, nargs='+')
    reader.set_defaults(command=analyze)
    arguments = parser.parse_args()
    arguments.command(arguments)


if __name__ == '__main__':
    main()
