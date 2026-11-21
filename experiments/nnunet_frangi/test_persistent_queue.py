import json
from pathlib import Path
import sys
import tempfile
import unittest
import hashlib
import os
import subprocess
from persistent_queue import run
from persistent_queue import classify_failure


class QueueTests(unittest.TestCase):
    def test_transient_retry_persisted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            counter = root / 'counter'
            worker = root / 'worker.py'
            worker.write_text('from pathlib import Path\n'
                + f'p=Path({str(counter)!r})\n'
                + 'n=int(p.read_text()) if p.exists() else 0\n'
                + 'p.write_text(str(n+1))\n'
                + 'print("Connection reset" if n==0 else "ok")\n'
                + 'raise SystemExit(1 if n==0 else 0)\n')
            spec = root / 'jobs.json'
            spec.write_text(json.dumps(dict(jobs=[dict(id='network', resumable=True,
                command=[sys.executable, str(worker)])])))
            queue = root / 'queue'
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('persistent_queue.py')),
                str(spec), '--work', str(queue)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(counter.read_text(), '2')
            self.assertEqual(json.loads((queue / 'retry_counts.json').read_text())['network'], 1)

    def test_dead_child_requires_explicit_recovery(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            spec = root / 'jobs.json'
            job = dict(id='interrupted', command=[sys.executable, '-c', 'pass'])
            spec.write_text(json.dumps(dict(jobs=[job])))
            child = subprocess.Popen([sys.executable, '-c', 'pass'])
            child.wait()
            queue = root / 'queue'
            queue.mkdir()
            signature = hashlib.sha256(json.dumps(job, sort_keys=True).encode()).hexdigest()
            state = dict(jobs={'interrupted': dict(status='running', pid=child.pid, signature=signature)})
            (queue / 'state.json').write_text(json.dumps(state))
            with self.assertRaisesRegex(RuntimeError, 'checkpoint recovery'):
                run(spec, queue)
            result = json.loads((queue / 'state.json').read_text())
            self.assertEqual(result['jobs']['interrupted']['status'], 'paused')

    def test_failure_categories(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / 'worker.log'
            log.write_text('CUDA out of memory')
            self.assertEqual(classify_failure(log), 'out_of_memory_no_configuration_change')
            log.write_text('ValueError: checksum mismatch')
            self.assertEqual(classify_failure(log), 'data_or_configuration')
            log.write_text('Connection reset')
            self.assertEqual(classify_failure(log), 'transient_network_candidate')

    def test_duplicate_queue_lock(self):
        import msvcrt
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            spec = root / 'jobs.json'
            spec.write_text(json.dumps({'jobs': []}))
            queue = root / 'queue'
            queue.mkdir()
            with (queue / 'queue.lock').open('a+b') as lock:
                lock.write(b'0')
                lock.flush()
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                result = subprocess.run([sys.executable, str(Path(__file__).with_name('persistent_queue.py')),
                    str(spec), '--work', str(queue)], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((queue / 'state.json').exists())

    def test_live_child_and_budget_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            spec = root / 'jobs.json'
            job = {'id': 'training', 'command': [sys.executable, '-c', 'pass']}
            spec.write_text(json.dumps({'jobs': [job]}))
            queue = root / 'queue'
            queue.mkdir()
            signature = hashlib.sha256(json.dumps(job, sort_keys=True).encode()).hexdigest()
            (queue / 'state.json').write_text(json.dumps({'jobs': {'training':
                dict(status='running', signature=signature, pid=os.getpid())}}))
            with self.assertRaisesRegex(RuntimeError, 'still alive'):
                run(spec, queue)
            job['estimated_gpu_hours'] = 61
            spec.write_text(json.dumps({'jobs': [job]}))
            with self.assertRaisesRegex(ValueError, '60 GPU'):
                run(spec, queue)

    def test_cooperative_pause(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            spec = root / 'jobs.json'
            spec.write_text(json.dumps({'jobs': [{'id': 'pause', 'resumable': True,
                'command': [sys.executable, '-c', 'import os; assert os.environ["LONG_RUN_STOP"]; raise SystemExit(75)']}]}))
            run(spec, root / 'queue')
            state = json.loads((root / 'queue/state.json').read_text())
            self.assertEqual(state['jobs']['pause']['status'], 'paused')

    def test_dependencies_resume_and_signature(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / 'result.txt'
            spec = root / 'jobs.json'
            jobs = [{'id': 'first', 'command': [sys.executable, '-c',
                     f'from pathlib import Path; Path({str(output)!r}).write_text("ok")'],
                     'outputs': [str(output)]},
                    {'id': 'second', 'depends': ['first'],
                     'command': [sys.executable, '-c', 'pass']}]
            spec.write_text(json.dumps({'jobs': jobs}))
            run(spec, root / 'queue')
            before = output.stat().st_mtime_ns
            run(spec, root / 'queue')
            self.assertEqual(before, output.stat().st_mtime_ns)
            jobs[0]['command'][-1] = 'pass'
            spec.write_text(json.dumps({'jobs': jobs}))
            with self.assertRaisesRegex(ValueError, 'signature'):
                run(spec, root / 'queue')

    def test_failure_and_stop(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            spec = root / 'jobs.json'
            spec.write_text(json.dumps({'jobs': [{'id': 'fail',
                'command': [sys.executable, '-c', 'raise SystemExit(3)']}]}))
            queue = root / 'queue'
            queue.mkdir()
            (queue / 'STOP').touch()
            run(spec, queue)
            self.assertFalse((queue / 'state.json').exists())
            (queue / 'STOP').unlink()
            with self.assertRaises(RuntimeError):
                run(spec, queue)
            self.assertEqual(json.loads((queue / 'state.json').read_text())['jobs']['fail']['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
