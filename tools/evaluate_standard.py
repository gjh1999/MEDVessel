"""Evaluate medvessel_export JSON/raw prefixes without resampling."""
import argparse
import json
from pathlib import Path
import numpy as np
import scipy
import skimage
from standard_vessel_metrics import PROFILE, evaluate


def read(prefix):
    path = Path(str(prefix) + '.json')
    meta = json.loads(path.read_text(encoding='utf-8'))
    if meta['dtype'] != 'float32_le':
        raise ValueError('Expected float32_le export')
    values = np.fromfile(path.parent / meta['raw_file'], dtype='<f4')
    return values.reshape(tuple(reversed(meta['dimensions_xyz']))), meta


def evaluate_exports(prediction, reference):
    a, ma = read(prediction)
    b, mb = read(reference)
    for key in ('dimensions_xyz', 'spacing_mm', 'origin_ras_mm', 'direction'):
        if not np.allclose(ma[key], mb[key], atol=1e-6, rtol=0):
            raise ValueError('Grid mismatch: ' + key)
    direction = np.asarray(ma['direction']).reshape(3, 3)
    if not np.allclose(direction.T @ direction, np.eye(3), atol=1e-6):
        raise ValueError('Distances require orthonormal grid directions')
    return {'profile': PROFILE, 'versions': {'numpy': np.__version__, 'scipy': scipy.__version__,
            'scikit_image': skimage.__version__},
            'metrics': evaluate(a, b, ma['spacing_mm'][::-1])}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prediction_prefix', type=Path)
    parser.add_argument('reference_prefix', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = evaluate_exports(args.prediction_prefix, args.reference_prefix)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
