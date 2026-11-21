"""Reuse immutable CT preprocessing with an isolated frozen subset split."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
NAME = 'Dataset511_CoronaryFrangi'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cases', type=int, choices=(35, 70), default=35)
    args = parser.parse_args()
    frozen = json.loads((HERE / 'work/long-run/frozen_splits.json').read_text())
    source = HERE / 'work/preprocessing-ablation/ct-only/nnUNet_preprocessed' / NAME
    train, val = frozen[f'train_{args.cases}'], frozen['validation_historical']
    assert len(train) == args.cases and len(val) == 15
    assert not set(train + val) & set(frozen['test_sealed'])
    work = HERE / f'work/long-run/learning-curve-{args.cases}'
    dest = work / 'nnUNet_preprocessed' / NAME
    dest.mkdir(parents=True, exist_ok=True)
    for name in ('nnUNetPlans.json', 'dataset_fingerprint.json', 'dataset.json'):
        target = dest / name
        if target.exists() and target.read_bytes() != (source / name).read_bytes():
            raise ValueError('Existing metadata mismatch')
        shutil.copy2(source / name, target)
    splits = [dict(train=train, val=val)]
    target = dest / 'splits_final.json'
    if target.exists() and json.loads(target.read_text()) != splits:
        raise ValueError('Existing split mismatch')
    target.write_text(json.dumps(splits, indent=2))
    linked = []
    for case in train + val:
        cache = source / 'nnUNetPlans_3d_fullres'
        members = list(cache.glob(case + '.*')) + list(cache.glob(case + '_seg.*'))
        if not any(p.suffix == '.pkl' for p in members) or not any(p.suffix in ('.npz', '.npy', '.b2nd') for p in members):
            raise ValueError('Missing cached data for ' + case)
        for origin in members:
            target = dest / cache.name / origin.name
            target.parent.mkdir(exist_ok=True)
            if not target.exists():
                os.link(origin, target)
            if not os.path.samefile(origin, target):
                raise ValueError('Cache not shared as expected')
            linked.append(dict(file=origin.name, bytes=origin.stat().st_size))
    plan = (dest / 'nnUNetPlans.json').read_bytes()
    manifest = dict(seed=frozen['seed'], train=train, validation=val,
        heldout_volumes_read=False, cached_files=linked, plans_sha256=hashlib.sha256(plan).hexdigest(),
        cache_policy='Read-only shared hardlinks; never overwrite cache files',
        schedule=dict(total_epochs=445, batches_per_epoch=250, first_prefix_epochs=50,
                      first_prefix_steps=12500, learning_rate_switch_epoch=245),
        status='prepared_not_trained')
    (work / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(dict(work=str(work), cases=len(train+val), cache_files=len(linked))))


if __name__ == '__main__':
    main()
