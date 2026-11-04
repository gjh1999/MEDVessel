"""Audit prepared smoke data and record repeatable evidence without any training."""
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone
import nibabel as nib
import numpy as np
from prepare_dataset import HERE, ROOT, check_grid, load_volume
from workflow import environment


def main():
    work = HERE / 'work'
    environment(work)
    from nnunetv2.training.dataloading.nnunet_dataset import infer_dataset_class
    raw = work / 'nnUNet_raw/Dataset512_CoronaryFrangiSmokeROI'
    processed = work / 'nnUNet_preprocessed' / raw.name
    plans = json.loads((processed / 'nnUNetPlans.json').read_text())
    configuration = plans['configurations']['3d_fullres']
    if configuration['normalization_schemes'] != ['CTNormalization', 'NoNormalization']:
        raise ValueError('Unexpected channel normalization')
    directory = processed / configuration['data_identifier']
    data = infer_dataset_class(str(directory))(str(directory))
    provenance = json.loads((raw / 'preparation.json').read_text())
    if len(data.identifiers) != len(provenance['cases']):
        raise ValueError('Missing preprocessed cases')
    for cid in data.identifiers:
        image, seg, _, _ = data.load_case(cid)
        array, labels = image[:], seg[:]
        if array.shape[0] != 2 or not np.isfinite(array).all() or array.shape[1:] != labels.shape[1:]:
            raise ValueError('Invalid preprocessed data: ' + cid)
        if not np.isin(labels, [-1, 0, 1]).all():
            raise ValueError('Invalid preprocessed labels')
    split = json.loads((processed / 'splits_final.json').read_text())[0]
    if set(split['train']) & set(split['val']) or set(split['train'] + split['val']) != set(data.identifiers):
        raise ValueError('Invalid split')
    enhanced, a = load_volume(ROOT / 'build/nnunet-frangi-enhancement-check.nii.gz')
    historical, b = load_volume(ROOT / 'build/imagecas-multicase/case-1/result/vesselness.nii.gz')
    check_grid(enhanced, historical)
    if not np.array_equal(a, b):
        raise ValueError('C++ enhancement changed')
    test = subprocess.run([sys.executable, str(HERE / 'test_pipeline.py')], check=True,
                          capture_output=True, text=True)
    (work / 'test_pipeline.log').write_text(test.stdout + test.stderr, encoding='utf-8')
    checkpoint_count = len(list(work.rglob('*.pth')))
    if checkpoint_count:
        raise ValueError('Unexpected trained checkpoint in verification work directory')
    report = {'checked_utc': datetime.now(timezone.utc).isoformat(),
              'scope': 'Development ROI data/geometry/preprocessing and untrained network forward verification only',
              'prepared_cases': len(data.identifiers), 'input_channels': 2,
              'official_nnunet_integrity_check': json.loads((work / 'verified_512.json').read_text()),
              'official_preprocessed_cases': len(data.identifiers),
              'normalization': configuration['normalization_schemes'],
              'internal_split': {'train': len(split['train']), 'validation': len(split['val']),
                                 'independent_test': 0},
              'enhancement_real_roi_voxelwise_identical_to_existing_cpp': True,
              'pipeline_unit_and_integration_tests_passed': 7,
              'full_volume_preparation_checked_on_synthetic_data': True,
              'full_volume_real_data_batch_prepared': False,
              'environment': json.loads((work / 'environment.json').read_text()),
              'forward': json.loads((work / 'forward_check.json').read_text()),
              'training_started': False, 'checkpoint_count': checkpoint_count,
              'deep_learning_segmentation_metrics': None,
              'not_yet_verified': ['5080 installation and execution', 'training/backpropagation',
                                  'real full-volume preparation and inference', 'held-out segmentation quality']}
    (HERE / 'validation_result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
