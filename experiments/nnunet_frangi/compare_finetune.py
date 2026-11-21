"""Compare fine-tuned checkpoints on validation only, with resumable predictions."""
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
BASE = HERE / 'work/full-real-100'
WORK = HERE / 'work/accuracy-finetune-01'


def record(stage, **details):
    target = WORK / 'comparison_status.json'
    temporary = target.with_suffix('.tmp')
    temporary.write_text(json.dumps(dict(stage=stage, pid=os.getpid(),
        updated_utc=datetime.now(timezone.utc).isoformat(), **details), indent=2))
    temporary.replace(target)


def main():
    import msvcrt
    with (WORK / 'comparison.lock').open('a+b') as lock:
        lock.write(b'0')
        lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            compare()
        except Exception as error:
            record('failed', error=str(error))
            raise


def compare():
    from tune_components import component_scores
    from prepare_dataset import load_volume, check_grid
    import numpy as np
    model = WORK / 'nnUNet_results/Dataset511_CoronaryFrangi/nnUNetTrainer__nnUNetPlans__3d_fullres'
    references = BASE / 'nnUNet_raw/Dataset511_CoronaryFrangi/labelsTr'
    cases = json.loads((BASE / 'validation_cases.json').read_text())
    candidates = []
    for checkpoint in ('checkpoint_final.pth', 'checkpoint_best.pth'):
        for mirror in (True, False):
            tag = checkpoint.removesuffix('.pth') + ('_tta' if mirror else '_plain')
            predictions = WORK / f'validation_{tag}'
            commands = [
                ('predict', 'predict.py', [model, BASE / 'validation_images', predictions,
                    '--work', BASE, '--checkpoint', checkpoint, '--resume'] +
                    (['--mirroring'] if mirror else [])),
                ('score', 'evaluate_predictions.py', [predictions, references,
                    WORK / f'metrics_{tag}.json', '--case-list', BASE / 'validation_cases.json'])]
            for stage, script, args in commands:
                record(f'{stage}_{tag}')
                with (WORK / f'{stage}_{tag}.log').open('a', encoding='utf-8') as log:
                    subprocess.run([sys.executable, '-u', str(HERE / script), *map(str, args)],
                        stdout=log, stderr=subprocess.STDOUT, check=True)
            metrics = json.loads((WORK / f'metrics_{tag}.json').read_text())
            thresholds = [0, 1, 5, 10, 25, 50, 100]
            records = []
            record(f'components_{tag}')
            for case in cases:
                image, pred = load_volume(predictions / f'{case}.nii.gz')
                ref_image, ref = load_volume(references / f'{case}.nii.gz')
                check_grid(image, ref_image)
                records.append(dict(case_id=case, candidates=component_scores(pred, ref,
                    abs(np.linalg.det(image.affine[:3, :3])), thresholds)))
            means = [dict(min_component_mm3=t, **{metric: float(np.mean([
                row['candidates'][i][metric] for row in records]))
                for metric in ('dice', 'precision', 'recall')}) for i, t in enumerate(thresholds)]
            (WORK / f'components_{tag}.json').write_text(json.dumps(dict(
                data_split='validation_only', cases=records, means=means,
                note='Exploratory only; masks unchanged. Review distal branch loss before adoption.'), indent=2))
            candidates.append(dict(checkpoint=checkpoint, mirroring=mirror,
                aggregates=metrics['aggregates'], lowest_dice_cases=sorted(
                    metrics['cases'], key=lambda row: row['dice'])[:5], component_candidates=means))
            (WORK / 'comparison_results.json').write_text(json.dumps(dict(
                data_split='validation_only', holdout_evaluated=False, candidates=candidates), indent=2))
    record('completed', results=str(WORK / 'comparison_results.json'), holdout_evaluated=False)


if __name__ == '__main__':
    main()
