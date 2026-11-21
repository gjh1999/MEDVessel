"""Freeze score-independent expanded scan cohorts and record identity limitations."""
import json
from pathlib import Path
import random

HERE = Path(__file__).resolve().parent
ROOT = HERE / 'work/long-run'


def main():
    inventory = ROOT / 'source-inventory'
    base = json.loads((ROOT / 'frozen_splits.json').read_text())
    labels = set(json.loads((inventory / 'labels_inventory.json').read_text())['label_ids'])
    def read(name):
        return set((inventory / name).read_text().split())
    official = {key: read(name) for key, name in (
        ('train', 'metadata_2_train.txt'), ('val', 'metadata_3_val.txt'),
        ('test', 'metadata_4_test.txt'), ('exclude', 'metadata_5_exclude.txt'))}
    local = {key: {cid.removeprefix('imagecas_') for cid in ids} for key, ids in (
        ('train', base['train_70']), ('val', base['validation_historical']), ('test', base['test_sealed']))}
    overlap = {a: {b: len(ids & other) for b, other in official.items()} for a, ids in local.items()}
    used = set.union(*local.values())
    rng = random.Random(base['seed'])
    # Preserve the historical local split; official metadata guides new allocations only.
    val_pool = sorted((official['val'] & labels) - used - official['exclude'], key=int)
    rng.shuffle(val_pool)
    if len(val_pool) < 35:
        raise ValueError('Insufficient new official-validation candidates')
    extra_val = val_pool[:35]
    train_pool = sorted((official['train'] & labels) - used - set(extra_val) - official['exclude'], key=int)
    rng.shuffle(train_pool)
    if len(train_pool) < 210:
        raise ValueError('Insufficient expanded training candidates')
    def names(ids):
        return sorted('imagecas_' + cid for cid in ids)
    train280 = local['train'] | set(train_pool[:210])
    val50 = local['val'] | set(extra_val)
    assert len(train280) == 280 and len(val50) == 50
    assert not train280 & val50 and not (train280 | val50) & local['test']
    report = dict(seed=base['seed'], train_35=base['train_35'], train_70=base['train_70'],
        train_140=names(local['train'] | set(train_pool[:70])), train_280=names(train280),
        validation_50=names(val50), test_sealed=base['test_sealed'],
        local_official_overlap=overlap,
        selection='Seeded without model scores; historical local split preserved; new train/val from corresponding official pools',
        patient_independence_verified=False,
        identity_limitation='Descriptors has Scan ID, Image Quality, Dominance, Disease only; no patient identity key. Scan uniqueness is not patient independence.',
        status='Frozen scan IDs; extraction, duplicate-content audit and input checks pending')
    target = ROOT / 'expanded_cohort.json'
    if target.exists() and json.loads(target.read_text()) != report:
        raise ValueError('Refusing to change an existing frozen cohort')
    target.write_text(json.dumps(report, indent=2))
    extraction = (train280 | val50) - used
    for low in (1, 201, 401, 601, 801):
        ids = sorted((cid for cid in extraction if low <= int(cid) <= low+199), key=int)
        (inventory / f'extract_{low}-{low+199}.json').write_text(json.dumps(ids))
    print(json.dumps(dict(train=280, validation=50, sealed_test=15,
                          new_scan_ids=len(extraction), overlap=overlap)))


if __name__ == '__main__':
    main()
