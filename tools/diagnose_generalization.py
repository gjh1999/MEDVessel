"""Audit fixed-protocol segmentation gates without altering algorithm outputs."""
import json
from pathlib import Path

import numpy as np
from scipy import ndimage

from plot_reference import read

ROOT = Path(__file__).resolve().parents[1]


def overlap(prediction, reference):
    tp = int(np.count_nonzero(prediction & reference))
    p = int(np.count_nonzero(prediction))
    r = int(np.count_nonzero(reference))
    return {'tp': tp, 'fp': p-tp, 'fn': r-tp, 'prediction_voxels': p, 'reference_voxels': r,
            'precision': tp/p if p else 0., 'recall': tp/r if r else 0.,
            'dice': 2*tp/(p+r) if p+r else 1.}


def grow(candidate, seed):
    components, _ = ndimage.label(candidate, structure=np.ones((3,3,3), dtype=bool))
    value = int(components[tuple(reversed(seed))])
    if value == 0:
        return np.zeros_like(candidate)
    result = components == value
    return result if np.count_nonzero(result) >= 20 else np.zeros_like(candidate)


def main():
    summary = json.loads((ROOT/'build/imagecas-multicase/summary.json').read_text())
    rows = []
    for entry in summary['evaluated_cases']:
        case = entry['case_id']
        directory = ROOT/'build/imagecas-multicase'/f'case-{case}'
        source, geometry = read(directory/'plots/source')
        vesselness, _ = read(directory/'plots/vesselness')
        reference = read(directory/'plots/reference')[0] > 0
        mask = read(directory/'plots/mask')[0] > 0
        seed = json.loads((directory/'roi.json').read_text())['seed_voxel']
        hu = np.isfinite(source) & (source >= 100) & (source <= 900)
        response = np.isfinite(vesselness) & (vesselness >= .08)
        candidate = hu & response
        reproduced = grow(candidate, seed)
        if not np.array_equal(reproduced, mask):
            raise ValueError(f'Independent seeded propagation differs from actual mask for case {case}')
        result = overlap(mask, reference)
        if abs(result['dice']-entry['dice']) > 1e-9:
            raise ValueError('Stored and recomputed Dice differ')
        r = result['reference_voxels']
        partitions = {
            'retained': int(np.count_nonzero(reference & mask)),
            'intensity_only_rejected': int(np.count_nonzero(reference & ~hu & response)),
            'response_only_rejected': int(np.count_nonzero(reference & hu & ~response)),
            'both_rejected': int(np.count_nonzero(reference & ~hu & ~response)),
            'eligible_but_unreachable': int(np.count_nonzero(reference & candidate & ~mask)),
        }
        assert sum(partitions.values()) == r
        modifications = {
            'baseline': result,
            'remove_intensity_gate': overlap(grow(response, seed), reference),
            'lower_response_gate_004': overlap(grow(hu & (vesselness >= .04), seed), reference),
            'all_candidates_no_seed_constraint': overlap(candidate, reference),
        }
        ref_components, nref = ndimage.label(reference, structure=np.ones((3,3,3)))
        row = {'case_id': case, 'previously_used_for_development': case == '1',
               'baseline': result, 'spacing_mm': geometry['spacing_mm'],
               'reference_components_26': int(nref),
               'seed_source_intensity': float(source[tuple(reversed(seed))]),
               'seed_response': float(vesselness[tuple(reversed(seed))]),
               'reference_intensity_percentiles_5_50_95': np.percentile(source[reference], [5,50,95]).tolist(),
               'reference_response_percentiles_5_50_95': np.percentile(vesselness[reference], [5,50,95]).tolist(),
               'reference_partitions_counts': partitions,
               'reference_partitions_fractions': {k:v/r for k,v in partitions.items()},
               'eligible_reference_fraction': float(np.count_nonzero(reference & candidate)/r),
               'diagnostic_counterfactuals': modifications,
               'independent_propagation_matches': True}
        rows.append(row)
        print(case, 'P/R/Dice', [round(result[x],4) for x in ['precision','recall','dice']],
              'GT partitions', {k:round(v/r,3) for k,v in partitions.items()}, flush=True)
    new = [r for r in rows if not r['previously_used_for_development']]
    averages = {variant: {metric:float(np.mean([r['diagnostic_counterfactuals'][variant][metric] for r in new]))
                for metric in ['dice','precision','recall']}
                for variant in rows[0]['diagnostic_counterfactuals']}
    output = {'protocol': 'Read-only audit of saved volumes; counterfactuals are retrospective diagnostics, not validated improvements',
              'cases': rows, 'additional_macro_means': averages,
              'additional_macro_partition_fractions': {k:float(np.mean([r['reference_partitions_fractions'][k] for r in new]))
                    for k in rows[0]['reference_partitions_fractions']}}
    path = ROOT/'build/imagecas-multicase/diagnostics.json'
    path.write_text(json.dumps(output, indent=2)+'\n')
    print(json.dumps({'additional_macro_means':averages, 'partitions':output['additional_macro_partition_fractions']}, indent=2))


if __name__ == '__main__':
    main()
