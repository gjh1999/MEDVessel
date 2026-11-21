"""Inventory actual historical prefixes; never impute full-volume scores."""
import csv
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
OUT = HERE / 'work/long-run/calibration'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    runs = []
    for relative in ('full-real-100', 'accuracy-finetune-01',
                     'preprocessing-ablation/ct-only', 'preprocessing-ablation/dual'):
        root = HERE / 'work' / relative
        epochs = {}
        for log in sorted(root.rglob('training_log*.txt')):
            current = None
            for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
                match = re.search(r': Epoch (\d+)\s*$', line)
                if match:
                    current = int(match[1])
                    epochs.setdefault(current, dict(epoch=current))
                if current is None:
                    continue
                for key, pattern in (
                    ('learning_rate', r'Current learning rate: ([\d.eE+-]+)'),
                    ('train_loss', r'train_loss ([\d.eE+-]+)'),
                    ('val_loss', r'val_loss ([\d.eE+-]+)'),
                    ('seconds', r'Epoch time: ([\d.]+)'),
                    ('patch_pseudo_dice', r'Pseudo dice.*?(-?\d+\.\d+)')):
                    value = re.search(pattern, line)
                    if value:
                        epochs[current][key] = float(value[1])
        fields = ['epoch', 'learning_rate', 'train_loss', 'val_loss', 'seconds', 'patch_pseudo_dice']
        with (OUT / (relative.replace('/', '_') + '.csv')).open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(epochs[k] for k in sorted(epochs))
        checkpoints = [dict(path=str(p.relative_to(HERE)), bytes=p.stat().st_size)
                       for p in root.rglob('checkpoint*.pth')]
        runs.append(dict(run=relative, recorded_epochs=len(epochs), checkpoints=checkpoints,
            prefix_observations={str(n): epochs.get(n-1) for n in (50, 100, 200, 445)},
            historical_prefix_full_volume_scores=None))
    report = dict(runs=runs, automatic_elimination_enabled=False,
        reason='Historical 50/100/200 full-volume validation not established; patch pseudo Dice is not a substitute. LR switches at epoch 245.',
        next_action='Preserve 445-epoch schedule, evaluate predefined prefixes prospectively; retain an exploration candidate.')
    (OUT / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({r['run']: r['recorded_epochs'] for r in runs}))


if __name__ == '__main__':
    main()
