"""Prepare the same two image-only channels for new full-volume cases; no labels accepted."""
import argparse
import json
from pathlib import Path
from prepare_dataset import HERE, ROOT, CONFIG, load_volume, check_grid, save_like, enhance
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path, help='A NEW directory containing the two nnU-Net channels')
    parser.add_argument('--case-id', default='new_case')
    parser.add_argument('--enhancer', type=Path, default=ROOT / 'build/cmake/experiments/nnunet_frangi/medvessel_enhance.exe')
    args = parser.parse_args()
    if not args.case_id or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in args.case_id):
        raise ValueError('Use a filename-safe case ID')
    args.output.mkdir(parents=True, exist_ok=False)
    source, ct = load_volume(args.image)
    vessel_path = args.output / f'{args.case_id}_0001.nii.gz'
    enhance(args.image, vessel_path, args.enhancer)
    vi, vessel = load_volume(vessel_path)
    check_grid(source, vi)
    if vessel.min() < -1e-6 or vessel.max() > 1.000001:
        raise ValueError('Invalid Frangi response range')
    save_like(ct.astype(np.float32), source, args.output / f'{args.case_id}_0000.nii.gz')
    (args.output / 'preparation.json').write_text(json.dumps({'frangi': CONFIG['frangi'],
        'labels_read': False, 'source': str(args.image.resolve())}, indent=2)+'\n')
    print('Prepared two image-only inference channels:', args.output)


if __name__ == '__main__':
    main()
