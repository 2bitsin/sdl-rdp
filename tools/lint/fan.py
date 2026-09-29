"""Per-item work fanned over the box's cores in worker processes, results in item order."""
import concurrent.futures
import math
import multiprocessing
import os
import pathlib

CHUNKS_PER_WORKER = 4
# A pool takes about 0.1 s to start its workers (measured on the 88-core box), more than a few dozen files cost.
SERIAL_BELOW      = 32
# fork deadlocked the suite when the pointers fixtures fanned out from their threads (measured 2026-09-30).
CONTEXT           = multiprocessing.get_context('forkserver' if 'forkserver' in multiprocessing.get_all_start_methods()
                                                else 'spawn')
CPU_MAX           = pathlib.Path('/sys/fs/cgroup/cpu.max')

_shared = ()


class ItemFailed(Exception):
    pass


def quota(cpu_max=CPU_MAX):
    """The cgroup v2 CPU quota in cores, rounded up; none when unlimited, absent or unreadable."""
    try:
        limit, period = cpu_max.read_text().split()
        return None if limit == 'max' else math.ceil(int(limit) / int(period))
    except (OSError, ValueError):
        return None


def worker_count(cpu_max=CPU_MAX):
    return min((count for count in (os.process_cpu_count(), quota(cpu_max)) if count is not None), default=1)


def _receive(shared):
    global _shared
    _shared = shared


def called(function, shared, item):
    """`function(*shared, item)`, a failure raised again naming the item."""
    try:
        return function(*shared, item)
    except Exception as error:
        raise ItemFailed(f'{item}: {type(error).__name__}: {error}') from error


def _call(function, item):
    return called(function, _shared, item)


def out(function, items, *shared):
    items   = list(items)
    workers = min(worker_count(), len(items))
    if workers <= 1 or len(items) < SERIAL_BELOW:
        return [called(function, shared, item) for item in items]
    chunk = -(-len(items) // (workers * CHUNKS_PER_WORKER))
    with concurrent.futures.ProcessPoolExecutor(workers, mp_context=CONTEXT, initializer=_receive,
                                                initargs=(shared,)) as pool:
        return list(pool.map(_call, [function] * len(items), items, chunksize=chunk))


def _collected(function, *arguments):
    return list(function(*arguments))


def flattened(function, items, *shared):
    return [result for results in out(_collected, items, function, *shared) for result in results]
