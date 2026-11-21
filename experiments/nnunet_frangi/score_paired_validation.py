"""Wait for paired training, then score validation copies with unified metrics."""
import json
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE / 'work/preprocessing-ablation'
BASE = HERE / 'work/full-real-100'


def record(stage, **details):
    path = WORK / 'scoring_status.json'
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(dict(stage=stage,
        updated_utc=datetime.now(timezone.utc).isoformat(), **details), indent=2))
    temp.replace(path)


def main():
    import msvcrt
    with (WORK / 'scoring.lock').open('a+b') as lock:
        lock.write(b'0')
        lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        while True:
            if (WORK / 'STOP_SCORING').exists():
                record('stopped')
                return
            state = json.loads((WORK / 'status.json').read_text())
            if state['stage'] == 'failed':
                raise RuntimeError('Training queue failed; inspect training logs')
            if state['stage'] == 'paired_training_completed':
                break
            record('waiting_for_paired_training', training=state)
            time.sleep(30)
        from predict import restore_prediction_units
        results = []
        cases = json.loads((BASE / 'validation_cases.json').read_text())
        for arm in ('ct-only', 'dual'):
            record('scoring', arm=arm)
            source = WORK / arm / 'nnUNet_results/Dataset511_CoronaryFrangi/nnUNetTrainer__nnUNetPlans__3d_fullres/fold_0/validation'
            if not (source / 'summary.json').exists():
                raise RuntimeError(f'Missing completed validation: {source}')
            copies = WORK / arm / 'unified_validation_predictions'
            copies.mkdir(exist_ok=True)
            for case in cases:
                shutil.copy2(source / f'{case}.nii.gz', copies / f'{case}.nii.gz')
            restore_prediction_units(BASE / 'validation_images', copies)
            output = WORK / arm / 'unified_metrics.json'
            subprocess.run([sys.executable, str(HERE / 'evaluate_predictions.py'),
                str(copies), str(BASE / 'nnUNet_raw/Dataset511_CoronaryFrangi/labelsTr'),
                str(output), '--case-list', str(BASE / 'validation_cases.json')], check=True)
            metrics = json.loads(output.read_text())
            results.append(dict(arm=arm, aggregates=metrics['aggregates'],
                lowest_dice_cases=sorted(metrics['cases'], key=lambda row: row['dice'])[:5]))
        (WORK / 'paired_validation_results.json').write_text(json.dumps(dict(
            data_split='validation_only', holdout_evaluated=False,
            checkpoint='final', mirroring=True, candidates=results), indent=2))
        record('completed', results=str(WORK / 'paired_validation_results.json'))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        record('failed', error=str(error))
        raise
