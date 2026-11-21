"""Keep the authorized CUDA training alive; resume checkpoints after failures."""
import json
import msvcrt
import os
from pathlib import Path
import subprocess
import sys
import time
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
WORK = HERE / 'work/full-real-100'


def main():
    # Windows releases this lock even if the guardian crashes.
    with (WORK / 'guardian.lock').open('a+b') as lock:
        lock.seek(0)
        if not lock.read(1):
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        attempt = 0

        def record(stage, **details):
            (WORK / 'guardian_status.json').write_text(json.dumps(dict(
                stage=stage, pid=os.getpid(), attempt=attempt,
                updated_utc=datetime.now(timezone.utc).isoformat(), **details), indent=2))

        while True:
            if (WORK / 'STOP_TRAINING').exists():
                record('stopped')
                return
            attempt += 1
            record('running')
            with (WORK / 'guardian_child.log').open('a', encoding='utf-8') as log:
                child = subprocess.Popen([sys.executable, '-u', str(HERE / 'resume_100.py')],
                                         stdout=log, stderr=subprocess.STDOUT)
                record('running', child_pid=child.pid)
                while child.poll() is None:
                    if (WORK / 'STOP_TRAINING').exists():
                        subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
                        child.wait()
                        record('stopped')
                        return
                    time.sleep(5)
            if child.returncode == 0:
                record('completed')
                return
            delay = min(300, 30 * 2 ** min(attempt - 1, 4))
            record('retry_wait', exit_code=child.returncode, retry_seconds=delay)
            for _ in range(delay):
                if (WORK / 'STOP_TRAINING').exists():
                    record('stopped')
                    return
                time.sleep(1)


if __name__ == '__main__':
    main()
