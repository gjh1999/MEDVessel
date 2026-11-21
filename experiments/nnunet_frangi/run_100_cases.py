"""Download 100 pairs, prepare full volumes, validate and start authorized CUDA training."""
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import time
import argparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--local-archive', type=Path)
    args = parser.parse_args()
    work = HERE / 'work/full-real-100'
    work.mkdir(parents=True, exist_ok=True)
    data = ROOT / 'datasets/imagecas-100'
    lock = work / 'PIPELINE_LOCK'
    lock.open('x').close()
    status = work / 'pipeline_status.json'
    def record(stage, **details):
        status.write_text(json.dumps({'updated_utc': datetime.now(timezone.utc).isoformat(),
                                     'stage': stage, **details}, indent=2) + '\n')
    def run(stage, script, options):
        record(stage)
        with (work / f'{stage}.log').open('a', encoding='utf-8') as log:
            subprocess.run([sys.executable, str(script), *map(str, options)], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    try:
        for attempt in range(3):
            try:
                run('download', ROOT / 'tools/fetch_imagecas_batch.py',
                    ['--target-cases', 100, '--output', data] +
                    (['--local-archive', args.local_archive] if args.local_archive else []))
                break
            except subprocess.CalledProcessError:
                if attempt == 2:
                    raise
                time.sleep(15)
        manifest = json.loads((data / 'manifest.json').read_text())
        if not manifest.get('complete') or len(manifest['cases']) != 100:
            raise ValueError('Expected exactly 100 downloaded paired cases')
        run('prepare', HERE / 'prepare_dataset.py', ['--mode', 'full-volume', '--data', data,
            '--work', work, '--enhancer', ROOT / 'build/nnunet-parallel/experiments/nnunet_frangi/medvessel_enhance.exe'])
        run('validate', HERE / 'validate_full_volume.py', ['--work', work, '--gpu-memory-gb', 8])
        run('training', HERE / 'train_when_ready.py', ['--work', work, '--execute-training'])
        record('completed')
    except Exception as error:
        record('failed', error=str(error))
        raise
    finally:
        lock.unlink()


if __name__ == '__main__':
    main()
