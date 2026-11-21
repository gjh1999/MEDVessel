"""Paired comparison of matching prospective prefix evaluations, no elimination."""
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent / 'work/long-run'


def main():
    runs = {}
    for size in (35, 70):
        folder = ROOT / f'learning-curve-{size}/evaluation_50'
        receipt = json.loads((folder / 'completed.json').read_text())
        metrics = json.loads((folder / 'metrics.json').read_text())
        if receipt['prefix'] != 50 or receipt['mirroring']:
            raise ValueError('Mismatched inference settings')
        runs[size] = dict(receipt=receipt, metrics=metrics)
    if runs[35]['receipt']['cases'] != runs[70]['receipt']['cases']:
        raise ValueError('Mismatched validation cases')
    cases = runs[35]['receipt']['cases']
    values = {size: {row['case_id']: row['dice'] for row in run['metrics']['cases']}
              for size, run in runs.items()}
    if any(set(value) != set(cases) for value in values.values()):
        raise ValueError('Incomplete per-case scores')
    delta = np.array([values[70][case]-values[35][case] for case in cases])
    rng = np.random.default_rng(42)
    bootstrap = delta[rng.integers(0, len(cases), size=(20000, len(cases)))].mean(axis=1)
    report = dict(epochs=50, optimization_steps=12500, mirroring=False,
        validation_cases=cases, mean_dice={str(size): float(np.mean(list(value.values())))
                                          for size, value in values.items()},
        paired_dice_70_minus_35=float(delta.mean()),
        exploratory_case_bootstrap_95_interval=np.quantile(bootstrap, [.025, .975]).tolist(),
        per_case=[dict(case=case, dice35=values[35][case], dice70=values[70][case],
                       difference=float(delta[i])) for i, case in enumerate(cases)],
        automatic_elimination=False,
        limitations='Eight reused, partly difficulty-selected development cases; one training seed; bootstrap excludes training-seed uncertainty. Prefix precedes LR switch at 245; no claim of final data-scale benefit.',
        decision='Retain candidates pending convergence and 140-case evidence; do not infer saturation from early prefix.')
    (ROOT / 'prefix50_comparison.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
