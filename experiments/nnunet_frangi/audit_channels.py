"""Development-only sampled channel audit and manifest duplicate scan check."""
import json
from pathlib import Path
import nibabel as nib
import numpy as np
from prepare_dataset import check_grid, CONFIG

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    base = HERE / 'work/full-real-100'
    cases = json.loads((base / 'validation_cases.json').read_text())[:3]
    records = []
    for case in cases:
        images = [nib.load(base / 'validation_images' / f'{case}_{channel:04d}.nii.gz')
                  for channel in range(2)]
        check_grid(*images)
        channels = []
        for image in images:
            if image.header.get_xyzt_units()[0] != 'mm':
                raise ValueError('Expected millimetres')
            # Sparse slice avoids a full float32 volume allocation.
            sample = np.asarray(image.dataobj[::8, ::8, image.shape[2]//2], dtype=np.float32)
            if not np.isfinite(sample).all():
                raise ValueError('Nonfinite sampled channel')
            channels.append(dict(shape=image.shape, spacing=nib.affines.voxel_sizes(image.affine).tolist(),
                sampled_min=float(sample.min()), sampled_max=float(sample.max()),
                sampled_nonzero_fraction=float(np.count_nonzero(sample)/sample.size)))
        records.append(dict(case=case, channels=channels))
        print(case, flush=True)
    source = json.loads((ROOT / 'datasets/imagecas-100/manifest.json').read_text())
    hashes = {}
    for row in source['cases']:
        hashes.setdefault(row['source_sha256'], []).append(row['case_id'])
    duplicates = [ids for ids in hashes.values() if len(ids)>1]
    result = dict(sampled_development_cases=records, frangi_configuration=CONFIG['frangi'],
        source_manifest_duplicate_hash_groups=duplicates,
        hash_scope='Existing manifest hashes only; full on-disk rehash pending',
        limitation='Sparse central slices only, not exhaustive image integrity or anatomical response audit',
        heldout_volumes_read=False)
    output = HERE / 'work/long-run/channel_audit.json'
    output.write_text(json.dumps(result, indent=2))
    print('Duplicate manifest hash groups:', len(duplicates))


if __name__ == '__main__':
    main()
