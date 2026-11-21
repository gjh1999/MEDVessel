"""Freeze a 70/15/15 split before nnU-Net fingerprinting."""
import json
import random


def reserve(dataset, provenance):
    if dataset.parents[1].name != 'full-real-100':
        return
    marker = dataset / 'holdout.json'
    if marker.exists():
        return
    records = provenance['cases']
    if len(records) != 100:
        raise ValueError('Expected 100 cases before reserving test data')
    old = {'1','100','102','103','106','107','109','11','110','111','112'}
    eligible = sorted(r['case_id'] for r in records if r['source_case_id'] not in old)
    random.Random(20261104).shuffle(eligible)
    test = sorted(eligible[:15])
    remaining = sorted(r['case_id'] for r in records if r['case_id'] not in test)
    random.Random(20261104).shuffle(remaining)
    val, train = sorted(remaining[:15]), sorted(remaining[15:])
    for folder in ('imagesTs', 'labelsTs'):
        (dataset / folder).mkdir(exist_ok=True)
    for cid in test:
        for channel in (0, 1):
            name = f'{cid}_{channel:04d}.nii.gz'
            (dataset / 'imagesTr' / name).rename(dataset / 'imagesTs' / name)
        (dataset / 'labelsTr' / f'{cid}.nii.gz').rename(dataset / 'labelsTs' / f'{cid}.nii.gz')
    metadata = json.loads((dataset / 'dataset.json').read_text())
    metadata['numTraining'] = 85
    (dataset / 'dataset.json').write_text(json.dumps(metadata, indent=2))
    (dataset / 'splits_final.json').write_text(json.dumps([{'train': train, 'val': val}], indent=2))
    marker.write_text(json.dumps({'train': train, 'val': val, 'test': test,
        'seed': 20261104, 'note': 'Internal holdout; patient independence unverified. Prior 11 development cases excluded from test.'}, indent=2))
