"""Prepare frozen 140/50 CT development data without touching sealed test images."""
import json
import os
from pathlib import Path
import shutil
from prepare_dataset import sha256, load_volume, save_like, check_grid
import nibabel as nib
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NAME = 'Dataset511_CoronaryFrangi'


def main():
    work = HERE / 'work/long-run'
    cohort = json.loads((work / 'expanded_cohort.json').read_text())
    train, val = cohort['train_140'], cohort['validation_50']
    assert len(train) == 140 and len(val) == 50
    assert not set(train) & set(val)
    assert not set(train+val) & set(cohort['test_sealed'])
    original = HERE / 'work/full-real-100/nnUNet_raw' / NAME
    dest = work / 'learning-curve-140/nnUNet_raw' / NAME
    dest.mkdir(parents=True, exist_ok=True)
    marker = dest / 'PREPARATION_INCOMPLETE'
    marker.touch()
    hashes = {}
    records = []
    for case in train+val:
        numeric = case.removeprefix('imagecas_')
        low = (int(numeric)-1)//200*200+1
        expansion = ROOT / f'datasets/learning-curve-expansion/{low}-{low+199}'
        source = original / 'imagesTr' / f'{case}_0000.nii.gz'
        label = original / 'labelsTr' / f'{case}.nii.gz'
        if not source.exists():
            source = expansion / f'{numeric}.img.nii.gz'
            label = expansion / f'{numeric}.coronary.nii.gz'
        if not source.is_file() or not label.is_file():
            raise FileNotFoundError('Missing frozen case ' + case)
        digest = sha256(source)
        if digest in hashes:
            raise ValueError('Duplicate CT content: ' + hashes[digest] + ', ' + case)
        hashes[digest] = case
        for origin, relative in ((source, Path('imagesTr') / f'{case}_0000.nii.gz'),):
            target = dest / relative
            target.parent.mkdir(exist_ok=True)
            if not target.exists():
                os.link(origin, target)
            if not os.path.samefile(origin, target):
                raise ValueError('Existing raw data provenance mismatch')
        label_image, label_data = load_volume(label)
        check_grid(nib.load(source), label_image)
        binary = (label_data > 0).astype(np.uint8)
        target_label = dest / 'labelsTr' / f'{case}.nii.gz'
        target_label.parent.mkdir(exist_ok=True)
        if target_label.exists():
            existing_image, existing_data = load_volume(target_label)
            check_grid(existing_image, label_image)
            if not np.array_equal(existing_data, binary):
                raise ValueError('Existing binary label mismatch')
        else:
            save_like(binary, label_image, target_label)
        records.append(dict(case=case, source_sha256=digest, label_sha256=sha256(label)))
        print(case, flush=True)
    processed = work / 'learning-curve-140/nnUNet_preprocessed' / NAME
    processed.mkdir(parents=True, exist_ok=True)
    cache = HERE / 'work/preprocessing-ablation/ct-only/nnUNet_preprocessed' / NAME
    metadata = json.loads((cache / 'dataset.json').read_text())
    metadata['numTraining'] = 190
    for folder in (dest, processed):
        (folder / 'dataset.json').write_text(json.dumps(metadata, indent=2))
    for name in ('nnUNetPlans.json', 'dataset_fingerprint.json'):
        shutil.copy2(cache / name, processed / name)
    (processed / 'splits_final.json').write_text(json.dumps([dict(train=train, val=val)]))
    manifest = dict(seed=cohort['seed'], train=train, validation=val, sources=records,
        heldout_volumes_read=False, patient_independence_verified=False,
        plans_policy='Frozen historical CT spatial plan and normalization; fingerprint retained as provenance, not recomputed on enlarged cohort')
    (work / 'learning-curve-140/manifest.json').write_text(json.dumps(manifest, indent=2))
    marker.unlink()


if __name__ == '__main__':
    main()
