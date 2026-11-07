# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Measure one owned child and retain actual exit, stdout, CPU and peak process RSS."""
import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any


def run(command, *, cwd, timeout=30) -> dict[str, Any]:
    started = time.perf_counter()
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        child = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr,
                                 start_new_session=True)
        timed_out = threading.Event()
        def stop():
            timed_out.set()
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        timer = threading.Timer(timeout, stop)
        timer.start()
        try:
            _, status, usage = os.wait4(child.pid, 0)
            child.returncode = os.waitstatus_to_exitcode(status)
        finally:
            timer.cancel()
        elapsed = (time.perf_counter() - started) * 1000
        stdout.seek(0); stderr.seek(0)
        output, errors = stdout.read().decode(), stderr.read().decode()
    # macOS reports bytes; Linux reports KiB. Do not call this live heap.
    rss = usage.ru_maxrss * (1 if sys.platform == 'darwin' else 1024)
    result = dict(command=command, exit_code=child.returncode, timed_out=timed_out.is_set(),
                  process_wall_ms=elapsed, process_cpu_user_ms=usage.ru_utime * 1000,
                  process_cpu_system_ms=usage.ru_stime * 1000, process_peak_rss_bytes=rss,
                  stdout=output, stderr=errors)
    records = [json.loads(line[7:]) for line in output.splitlines() if line.startswith('RESULT ')]
    if len(records) == 1:
        result['measurement'] = records[0]
    elif records:
        raise ValueError('Multiple workload results in one child')
    return result


def normalize(record: dict[str, Any], library, mode, target) -> dict[str, Any]:
    data = record.get('measurement')
    if data:
        count = data['operations']
        if data['checksum'] != count * (count + 1) // 2:
            raise ValueError('Wrong workload checksum')
        if mode != 'memory':
            for key in data:
                if key.startswith(('cpp_', 'cobalt_frame_')):
                    data[key] = None
        elif library != 'cobalt':
            for key in data:
                if key.startswith('cobalt_frame_'):
                    data[key] = None
        if target == 'native':
            data['linear_memory_before'] = None
            data['linear_memory_after'] = None
    return record
