"""Validation-only component filtering study; never modify masks or inspect test data."""
import json
from pathlib import Path
import numpy as np
from scipy import ndimage
from prepare_dataset import load_volume, check_grid


def component_scores(prediction, reference, voxel_mm3, thresholds):
    components, _ = ndimage.label(prediction > 0, ndimage.generate_binary_structure(3, 1))
    sizes = np.bincount(components.ravel())
    overlap = np.bincount(components[reference > 0], minlength=len(sizes))
    reference_size = np.count_nonzero(reference)
    rows = []
    for threshold in thresholds:
        keep = sizes * voxel_mm3 >= threshold
        keep[0] = False
        predicted = int(sizes[keep].sum())
        tp = int(overlap[keep].sum())
        rows.append(dict(min_component_mm3=threshold,
                         dice=2 * tp / (predicted + reference_size) if predicted + reference_size else 1.,
                         precision=tp / predicted if predicted else float(reference_size == 0),
                         recall=tp / reference_size if reference_size else 1.))
    return rows


def main():
    work = Path(__file__).resolve().parent / 'work/full-real-100'
    cases = json.loads((work / 'validation_cases.json').read_text())
    thresholds = [0, 1, 5, 10, 25, 50, 100]
    records = []
    for case in cases:
        image, pred = load_volume(work / 'validation_checkpoint_final_plain' / f'{case}.nii.gz')
        ref_image, ref = load_volume(work / 'nnUNet_raw/Dataset511_CoronaryFrangi/labelsTr' / f'{case}.nii.gz')
        check_grid(image, ref_image)
        scores = component_scores(pred, ref, abs(np.linalg.det(image.affine[:3, :3])), thresholds)
        records.append(dict(case_id=case, candidates=scores))
        print(case, scores, flush=True)
    means = [dict(min_component_mm3=t, **{metric: float(np.mean([r['candidates'][i][metric] for r in records]))
              for metric in ('dice', 'precision', 'recall')}) for i, t in enumerate(thresholds)]
    result = dict(stage='completed', data_split='validation_only', cases=records, means=means,
                  note='Exploratory validation tuning. No test data used. No masks modified. '
                       'Small-component removal may remove true distal branches; review connectivity before adoption.')
    (work / 'component_tuning.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(means, indent=2), flush=True)


if __name__ == '__main__':
    main()
