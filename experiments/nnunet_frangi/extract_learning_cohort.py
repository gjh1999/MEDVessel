"""Extract frozen 140-case expansion groups with conservative disk preflight."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--budget-only', action='store_true')
    args = parser.parse_args()
    work = HERE / 'work/long-run'
    source = json.loads((ROOT / 'datasets/imagecas-100/manifest.json').read_text())
    cohort = json.loads((work / 'expanded_cohort.json').read_text())
    base = json.loads((work / 'frozen_splits.json').read_text())
    known = set(base['train_70'] + base['validation_historical'] + base['test_sealed'])
    needed = set(cohort['train_140'] + cohort['validation_50']) - known
    cache = HERE / 'work/preprocessing-ablation/ct-only/nnUNet_preprocessed/Dataset511_CoronaryFrangi/nnUNetPlans_3d_fullres'
    cache_bytes = sum(p.stat().st_size for p in cache.iterdir() if p.is_file())
    mean_source = sum(row['source_bytes'] for row in source['cases']) / len(source['cases'])
    # Allow compressed cache + an additional uncompressed float image and label.
    plans = json.loads((cache.parent / 'nnUNetPlans.json').read_text())
    shape = plans['configurations']['3d_fullres']['median_image_size_in_voxels']
    voxels = shape[0]*shape[1]*shape[2]
    required = int(len(needed)*(mean_source + cache_bytes/85 + voxels*6) + 20*2**30)
    free = shutil.disk_usage(work).free
    report = dict(new_cases=len(needed), required_bytes=required, free_bytes=free,
        cache_measurement_bytes=cache_bytes, safety_bytes=20*2**30,
        basis='Measured compressed source/cache averages plus 6 bytes per median voxel, 20 GiB safety; projected, not guaranteed',
        sufficient=free >= required)
    (work / 'expansion140_budget.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
    if args.budget_only:
        return
    if free < required:
        raise RuntimeError('Insufficient projected expansion space')
    if not args.archive.is_file():
        raise FileNotFoundError(args.archive)
    for low in (1, 201, 401, 601, 801):
        group = f'{low}-{low+199}'
        selection = work / f'source-inventory/extract140_{group}.json'
        if not json.loads(selection.read_text()):
            continue
        command = [sys.executable, '-u', str(ROOT / 'tools/fetch_imagecas_batch.py'),
            '--local-archive', str(args.archive), '--group', group, '--case-list', str(selection),
            '--output', str(ROOT / f'datasets/learning-curve-expansion/{group}')]
        local_labels = work / 'source-inventory/expansion_labels.zip'
        if local_labels.is_file():
            command += ['--local-labels', str(local_labels)]
        subprocess.run(command, check=True)
    (work / 'expansion_extraction_completed.json').write_text(json.dumps(dict(
        new_cases=len(needed), duplicate_content_audit_pending=True)))


if __name__ == '__main__':
    main()
