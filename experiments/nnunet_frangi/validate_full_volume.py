"""Verify, plan, preprocess and forward-check prepared full CTA data; never train."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import time

from workflow import HERE, dataset_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--dataset', type=int, default=511)
    parser.add_argument('--gpu-memory-gb', type=float, default=3.)
    parser.add_argument('--wait-for-preparation-seconds', type=int, default=0)
    args = parser.parse_args()
    deadline = time.monotonic() + args.wait_for_preparation_seconds
    while True:
        matches = list((args.work / 'nnUNet_raw').glob(f'Dataset{args.dataset:03d}_*'))
        incomplete = len(matches) == 1 and (matches[0] / 'PREPARATION_INCOMPLETE').exists()
        if not incomplete:
            break
        if time.monotonic() >= deadline:
            raise ValueError('Preparation is still incomplete; see preparation process output')
        time.sleep(10)
    dataset, provenance = dataset_path(args.work, args.dataset)
    if provenance['mode'] != 'full-volume' or provenance['reference_guided_roi']:
        raise ValueError('Expected full-volume data without reference-guided crops')
    if provenance.get('data_kind') == 'synthetic':
        raise ValueError('Use validate_synthetic.py for synthetic fixtures')
    marker = args.work / 'FULL_VALIDATION_INCOMPLETE'
    marker.write_text('Full-volume workflow has not completed.\n')
    stages = [['check'], ['verify', '--dataset', str(args.dataset)],
              ['plan', '--dataset', str(args.dataset), '--gpu-memory-gb', str(args.gpu_memory_gb)],
              ['forward', '--dataset', str(args.dataset), '--device', 'cpu']]
    for options in stages:
        log = args.work / f'full_{options[0]}.log'
        print('Starting:', options[0], flush=True)
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run([sys.executable, str(HERE / 'workflow.py'), *options,
                                     '--work', str(args.work)], stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{options[0]} failed; see {log}')
        print('Completed:', options[0], flush=True)
    processed = args.work / 'nnUNet_preprocessed' / dataset.name
    plans = json.loads((processed / 'nnUNetPlans.json').read_text())
    normalization = plans['configurations']['3d_fullres']['normalization_schemes']
    if normalization != ['CTNormalization', 'NoNormalization']:
        raise ValueError('Unexpected channel normalization')
    report = {'checked_utc': datetime.now(timezone.utc).isoformat(), 'dataset': dataset.name,
              'case_count': len(provenance['cases']), 'normalization': normalization,
              'environment': json.loads((args.work / 'environment.json').read_text()),
              'forward': json.loads((args.work / 'forward_check.json').read_text()),
              'training_started': False, 'segmentation_accuracy': None}
    (args.work / 'full_validation.json').write_text(json.dumps(report, indent=2) + '\n')
    marker.unlink()
    print('Full-volume validation completed:', args.work, flush=True)


if __name__ == '__main__':
    main()
