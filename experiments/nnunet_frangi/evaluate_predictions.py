"""Score actual predicted NIfTI masks with the project's declared paper metric profile."""
import argparse
import json
from pathlib import Path
from statistics import mean, stdev
import sys
import numpy as np
from prepare_dataset import load_volume, check_grid, ROOT
sys.path.insert(0, str(ROOT / 'tools'))
from standard_vessel_metrics import PROFILE, evaluate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('predictions', type=Path)
    parser.add_argument('references', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--case-list', type=Path, required=True,
                        help='JSON list of expected IDs; missing predictions must not disappear from the evaluation')
    args = parser.parse_args()
    expected = json.loads(args.case_list.read_text())
    if not expected or len(set(expected)) != len(expected):
        raise ValueError('Expected a nonempty unique list of case IDs')
    rows = []
    for cid in expected:
        pi, prediction = load_volume(args.predictions / f'{cid}.nii.gz')
        ri, reference = load_volume(args.references / f'{cid}.nii.gz')
        check_grid(pi, ri)
        if not np.isin(prediction, [0, 1]).all() or not np.isin(reference, [0, 1]).all():
            raise ValueError('Expected binary masks, not probabilities or multiclass arrays')
        # Nibabel arrays are XYZ; the metric takes a spacing for each corresponding axis.
        metrics = evaluate(prediction.transpose(2, 1, 0), reference.transpose(2, 1, 0),
                           np.asarray(nib_spacing(ri))[::-1])
        rows.append({'case_id': cid, **metrics})
    aggregates = {}
    for key in ('dice', 'hd95_mm', 'assd_mm', 'cldice', 'precision', 'recall'):
        values = [r[key] for r in rows if r[key] is not None]
        aggregates[key] = {'mean_defined': mean(values) if values else None,
                           'sample_std_defined': stdev(values) if len(values) > 1 else None,
                           'defined_cases': len(values), 'undefined_cases': len(rows) - len(values)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'profile': PROFILE, 'case_count': len(rows), 'cases': rows,
        'aggregates': aggregates, 'note': 'Defined-distance means exclude nulls explicitly; always report undefined case count.'},
        indent=2, allow_nan=False)+'\n')


def nib_spacing(image):
    from nibabel.affines import voxel_sizes
    return voxel_sizes(image.affine)


if __name__ == '__main__':
    main()
