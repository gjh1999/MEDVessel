"""Audit immutable prefix-50 evidence, then queue retained candidates to epoch 100."""
import json
from pathlib import Path
import sys
import torch
from prepare_dataset import sha256

HERE = Path(__file__).resolve().parent
WORK = HERE / 'work/long-run'


def main():
    path = WORK / 'batch01_with_evaluation.json'
    spec = json.loads(path.read_text())
    audits = []
    for size in (140, 70, 35):
        work = WORK / f'learning-curve-{size}'
        folder = work / 'nnUNet_results/Dataset511_CoronaryFrangi/nnUNetTrainer__nnUNetPlans__3d_fullres/fold_0'
        checkpoint = folder / 'checkpoint_latest.pth'
        evidence = json.loads((work / 'evaluation_50/completed.json').read_text())
        snapshot = folder / 'checkpoint_prefix_50.pth'
        if sha256(snapshot) != evidence['checkpoint_sha256']:
            raise ValueError('Immutable prefix snapshot mismatch')
        state = torch.load(checkpoint, map_location='cpu', weights_only=False)
        if state['current_epoch'] != 50 or not state['optimizer_state']:
            raise ValueError('Expected epoch-50 optimizer checkpoint before scheduling')
        audits.append(dict(cases=size, checkpoint_epoch=50, snapshot_verified=True))
    previous = 'ct140_evaluate50'
    existing = {job['id'] for job in spec['jobs']}
    for size in (140, 70, 35):
        work = WORK / f'learning-curve-{size}'
        for stage, script, output, hours in (
            ('prefix', 'train_learning_curve.py', work / 'prefix_100.json', 1.5),
            ('evaluate', 'evaluate_learning_prefix.py', work / 'evaluation_100/completed.json', .5)):
            identifier = f'ct{size}_{stage}100'
            if identifier not in existing:
                spec['jobs'].append(dict(id=identifier, depends=[previous],
                    command=[sys.executable, '-u', str(HERE / script), '--work', str(work), '--prefix', '100'],
                    outputs=[str(output)], gpu=True, estimated_gpu_hours=hours,
                    resumable=True, minimum_free_bytes=10*2**30))
            previous = identifier
    (WORK / 'prefix100_decision.json').write_text(json.dumps(dict(audits=audits,
        decision='Retain all three candidates, prioritize 140-case signal; fixed cumulative 25000 steps, same 445-epoch LR endpoint and eight-case plain validation.',
        automatic_elimination=False, expected_additional_gpu_hours=6,
        uncertainty='Single-seed early prefixes; no final data-scale conclusion.'), indent=2))
    path.write_text(json.dumps(spec, indent=2))
    print('Audited three prefix-50 checkpoints; queued 140, 70, 35 to cumulative epoch 100')


if __name__ == '__main__':
    main()
