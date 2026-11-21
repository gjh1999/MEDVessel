"""Serial subprocess queue with immutable jobs and restartable atomic state."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def process_alive(pid):
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            # Access denied is not evidence that the process is gone.
            return ctypes.get_last_error() == 5
        code = wintypes.DWORD()
        ok = kernel.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel.CloseHandle(handle)
        return not ok or code.value == 259
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def save(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2), encoding='utf-8')
    temp.replace(path)


def classify_failure(log_path):
    with log_path.open('rb') as stream:
        stream.seek(max(0, log_path.stat().st_size - 65536))
        tail = stream.read().decode('utf-8', errors='replace').lower()
    if 'out of memory' in tail:
        return 'out_of_memory_no_configuration_change'
    if any(word in tail for word in ('checksum', 'integrity', 'signature mismatch', 'filenotfounderror', 'valueerror')):
        return 'data_or_configuration'
    if any(word in tail for word in ('timed out', 'connection reset', 'temporary failure')):
        return 'transient_network_candidate'
    return 'process_failure_requires_diagnosis'


def run(spec, work, wait_lock=False):
    import msvcrt
    work.mkdir(parents=True, exist_ok=True)
    with (work / 'queue.lock').open('a+b') as lock:
        lock.write(b'0')
        lock.flush()
        lock.seek(0)
        while True:
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                if not wait_lock:
                    raise
                if (work / 'STOP').exists():
                    return
                time.sleep(5)
        jobs = json.loads(spec.read_text())['jobs']
        ids = [job['id'] for job in jobs]
        if len(set(ids)) != len(ids):
            raise ValueError('Duplicate job IDs')
        for i, job in enumerate(jobs):
            if not set(job.get('depends', [])).issubset(ids[:i]):
                raise ValueError('Dependencies must precede job')
        state_path = work / 'state.json'
        state = json.loads(state_path.read_text()) if state_path.exists() else {'jobs': {}}
        state.setdefault('gpu_seconds', 0)
        planned = sum(job.get('estimated_gpu_hours', 0) for job in jobs
                      if state['jobs'].get(job['id'], {}).get('status') != 'completed')
        if planned + state['gpu_seconds'] / 3600 > 60:
            raise ValueError('Batch exceeds persistent 60 GPU-hour ceiling')
        for job in jobs:
            signature = hashlib.sha256(json.dumps(job, sort_keys=True).encode()).hexdigest()
            old = state['jobs'].get(job['id'], {})
            if old.get('status') == 'running':
                if old.get('pid') and process_alive(old['pid']):
                    raise RuntimeError('Previous child still alive; refusing duplicate launch')
                old.update(status='paused', reason='Queue interrupted; child no longer alive')
                save(state_path, state)
                if not job.get('resumable', False):
                    raise RuntimeError('Interrupted job requires explicit checkpoint recovery')
            if old and old['signature'] != signature:
                raise ValueError('Old output/configuration signature mismatch')
            outputs = [Path(p) for p in job.get('outputs', [])]
            if old.get('status') == 'completed':
                if not all(p.is_file() and p.stat().st_size for p in outputs):
                    raise ValueError('Completed output missing or empty')
                continue
            if any(state['jobs'].get(dep, {}).get('status') != 'completed'
                   for dep in job.get('depends', [])):
                raise ValueError('Dependency incomplete')
            if (work / 'STOP').exists():
                return
            if shutil.disk_usage(work).free < job.get('minimum_free_bytes', 2**30):
                raise RuntimeError('Insufficient disk space')
            if not old and any(p.exists() for p in outputs):
                raise ValueError('Untracked old output refused')
            record = dict(signature=signature, status='running', started=time.time())
            state['jobs'][job['id']] = record
            save(state_path, state)
            with (work / (job['id'] + '.log')).open('ab') as log:
                env = os.environ.copy()
                env['LONG_RUN_STOP'] = str((work / 'STOP').resolve())
                child = subprocess.Popen(job['command'], stdout=log, stderr=subprocess.STDOUT, env=env)
                record['pid'] = child.pid
                last_tick = time.monotonic()
                while child.poll() is None:
                    now = time.monotonic()
                    if job.get('gpu', False):
                        state['gpu_seconds'] += now - last_tick
                    last_tick = now
                    if state['gpu_seconds'] >= 60 * 3600:
                        (work / 'STOP').touch()
                    record['heartbeat'] = time.time()
                    record['stop_requested'] = (work / 'STOP').exists()
                    # Cooperative workers must save checkpoints and exit at boundaries.
                    save(state_path, state)
                    time.sleep(.5)
            record.update(returncode=child.returncode, finished=time.time())
            record['status'] = ('paused' if child.returncode == 75 else 'completed' if child.returncode == 0 and
                all(p.is_file() and p.stat().st_size for p in outputs) else 'failed')
            if record['status'] == 'failed':
                record['failure_class'] = classify_failure(work / (job['id'] + '.log'))
            save(state_path, state)
            if record['status'] == 'paused':
                return
            if record['status'] != 'completed':
                raise RuntimeError('Job failed: ' + job['id'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('spec', type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--wait-lock', action='store_true')
    args = parser.parse_args()
    for retry in range(4):
        try:
            run(args.spec, args.work, args.wait_lock)
            break
        except RuntimeError:
            state_path = args.work / 'state.json'
            if not state_path.exists() or retry == 3:
                raise
            state = json.loads(state_path.read_text())
            failed = [key for key, row in state['jobs'].items()
                      if row.get('status') == 'failed' and row.get('failure_class') == 'transient_network_candidate']
            jobs = json.loads(args.spec.read_text())['jobs']
            eligible = [job for job in jobs if job['id'] in failed and job.get('resumable')]
            if not eligible or (args.work / 'STOP').exists():
                raise
            # Persist across restarts; a new CLI invocation does not reset retry allowance.
            retries_path = args.work / 'retry_counts.json'
            counts = json.loads(retries_path.read_text()) if retries_path.exists() else {}
            for job in eligible:
                key = job['id']
                if counts.get(key, 0) >= 3:
                    raise
                counts[key] = counts.get(key, 0) + 1
            save(retries_path, counts)
            time.sleep(min(30, 2 ** max(counts[job['id']] for job in eligible)))
