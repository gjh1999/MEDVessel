"""Resume the interrupted authorized run, then continue evaluation."""
import json
import subprocess
import sys
from datetime import datetime, timezone
from workflow import HERE, environment, command_path

work = HERE / 'work/full-real-100'


def record(stage, **details):
    data = {'updated_utc': datetime.now(timezone.utc).isoformat(), 'stage': stage, **details}
    (work / 'pipeline_status.json').write_text(json.dumps(data, indent=2))
    (work / 'training_status.json').write_text(json.dumps(data, indent=2))


if __name__ == '__main__':
    try:
        record('training', training_started=True, resumed=True)
        train_env = environment(work)
        tuning = work / 'training_performance.json'
        if tuning.exists():
            workers = int(json.loads(tuning.read_text())['augmentation_workers'])
            if not 1 <= workers <= 8:
                raise ValueError('augmentation_workers must be between 1 and 8')
            train_env['nnUNet_n_proc_DA'] = str(workers)
        train_env['MEDVESSEL_NEWEST_CHECKPOINT'] = '1'
        final_exists = any((work / 'nnUNet_results').rglob('checkpoint_final.pth'))
        validation_done = any((work / 'nnUNet_results').rglob('validation/summary.json'))
        if not (final_exists and validation_done):
            with (work / 'training.log').open('a', encoding='utf-8') as log:
                subprocess.run([sys.executable, '-u', str(HERE / 'train_with_early_stopping.py'), '511', '3d_fullres', '0', '--npz',
                                '-device', 'cuda', '--val' if final_exists else '--c'], env=train_env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        record('completed', training_started=True)
        subprocess.run([sys.executable, str(HERE / 'finish_experiment.py')], check=True)
    except Exception as error:
        record('failed', error=str(error))
        raise
