"""Exercise C++ enhancement and official nnU-Net on synthetic data, without training."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

import nibabel as nib
import numpy as np

from prepare_dataset import HERE, ROOT, prepare, save_like, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True, help='New directory for synthetic evidence only')
    parser.add_argument('--enhancer', type=Path, default=ROOT / 'build/cmake/experiments/nnunet_frangi/medvessel_enhance.exe')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=False)
    marker = args.work / 'VALIDATION_INCOMPLETE'
    marker.write_text('Synthetic validation has not completed.\n')
    sources = args.work / 'sources'
    sources.mkdir()
    xyz = np.indices((48, 48, 48), dtype=np.float32)
    affine = np.diag([-.7, .7, 1., 1.])
    affine[:3, 3] = [40, -20, 10]
    records = []
    for index in range(3):
        radius2 = (xyz[0] - 23 - index) ** 2 + (xyz[1] - 24) ** 2
        ct = (-100 + 600 * np.exp(-radius2 / 10)).astype(np.float32)
        label = (radius2 < 9).astype(np.uint8)
        image = nib.Nifti1Image(ct, affine)
        source = sources / f'{index}.nii.gz'
        reference = sources / f'{index}.label.nii.gz'
        save_like(ct, image, source)
        save_like(label, image, reference)
        records.append({'case_id': str(index), 'source_file': source.name,
                        'label_file': reference.name, 'source_sha256': sha256(source),
                        'label_sha256': sha256(reference)})
    (sources / 'manifest.json').write_text(json.dumps({'cases': records, 'data_kind': 'synthetic'}, indent=2))
    prepare(argparse.Namespace(mode='full-volume', data=sources, work=args.work,
                               cases=None, enhancer=args.enhancer))
    commands = [['check'], ['verify', '--dataset', '511'],
                ['plan', '--dataset', '511', '--gpu-memory-gb', '3'],
                ['forward', '--dataset', '511', '--device', 'cpu']]
    for options in commands:
        completed = subprocess.run([sys.executable, str(HERE / 'workflow.py'), *options,
                                    '--work', str(args.work)], capture_output=True, text=True)
        (args.work / f'{options[0]}.log').write_text(completed.stdout + completed.stderr, encoding='utf-8')
        if completed.returncode:
            raise RuntimeError(f'Synthetic {options[0]} failed; see {args.work / (options[0] + ".log")}')
        print('Completed synthetic stage:', options[0], flush=True)
    report = {'checked_utc': datetime.now(timezone.utc).isoformat(),
              'scope': 'Synthetic end-to-end execution only; no real patient data or accuracy evidence',
              'training_started': False,
              'environment': json.loads((args.work / 'environment.json').read_text()),
              'integrity': json.loads((args.work / 'verified_511.json').read_text()),
              'forward': json.loads((args.work / 'forward_check.json').read_text())}
    (args.work / 'synthetic_validation.json').write_text(json.dumps(report, indent=2) + '\n')
    marker.unlink()
    print('Synthetic validation complete:', args.work)


if __name__ == '__main__':
    main()
