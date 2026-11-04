"""Create CT + image-only Frangi nnU-Net v2 datasets; never start training."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess

import nibabel as nib
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CONFIG = json.loads((HERE / 'config.json').read_text())


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def load_volume(path):
    image = nib.load(path)
    if len(image.shape) != 3 or image.header.get_xyzt_units()[0] != 'mm':
        raise ValueError(f'Expected 3-D image in millimetres: {path}')
    spacing = nib.affines.voxel_sizes(image.affine)
    if not np.isfinite(image.affine).all() or np.any(spacing <= 0):
        raise ValueError('Invalid affine')
    axes = image.affine[:3, :3] / spacing
    if not np.allclose(axes.T @ axes, np.eye(3), atol=1e-5):
        raise ValueError('Sheared grid is not supported')
    data = image.get_fdata(dtype=np.float32)
    if not np.isfinite(data).all():
        raise ValueError(f'Nonfinite voxels: {path}')
    return image, data


def check_grid(a, b):
    if a.shape != b.shape or not np.allclose(a.affine, b.affine, atol=1e-4, rtol=0):
        raise ValueError('Image/channel/label grids do not match; no implicit resampling')


def save_like(data, image, destination):
    out = nib.Nifti1Image(data, image.affine, header=image.header.copy())
    out.set_data_dtype(data.dtype)
    out.header.set_xyzt_units('mm')
    # Preserve physical geometry, including reflected axes. sform is authoritative.
    out.set_sform(image.affine, code=1)
    out.set_qform(image.affine, code=1)
    nib.save(out, destination)


def enhance(source, destination, executable):
    parameters = CONFIG['frangi']
    command = [str(executable), str(source), str(destination), '--scales',
               ','.join(map(str, parameters['scales_mm']))]
    for key in ('alpha', 'beta', 'c'):
        command += ['--' + key, str(parameters[key])]
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/ucrt64/bin;' + env.get('PATH', '')
    env.setdefault('OMP_NUM_THREADS', '4')
    subprocess.run(command, check=True, env=env)


def fixed_split(ids):
    if len(ids) < 3 or len(set(ids)) != len(ids):
        raise ValueError('At least three unique case IDs required')
    ordered = sorted(ids)
    random.Random(CONFIG['split_seed']).shuffle(ordered)
    count = max(1, round(len(ordered) * .25))
    return [{'train': sorted(ordered[count:]), 'val': sorted(ordered[:count])}]


def prepare(args):
    mode = args.mode
    prefix = 'smoke' if mode == 'smoke-roi' else 'full'
    name = f'Dataset{CONFIG[prefix + "_dataset_id"]:03d}_{CONFIG[prefix + "_dataset_name"]}'
    directory = args.work / 'nnUNet_raw' / name
    if directory.exists():
        raise FileExistsError(f'Use a new work directory; refusing partial overwrite: {directory}')
    manifest = json.loads((args.data / 'manifest.json').read_text())
    selected = manifest['cases']
    if args.cases:
        wanted = set(args.cases.split(','))
        selected = [c for c in selected if c['case_id'] in wanted]
        if {c['case_id'] for c in selected} != wanted:
            raise ValueError('Requested IDs missing from manifest')
    if len(selected) < 3:
        raise ValueError('At least three cases required for the development split')
    directory.mkdir(parents=True)
    for child in ('imagesTr', 'labelsTr'):
        (directory / child).mkdir()
    records = []
    # Write incomplete marker first; workflow.py refuses this dataset until completed.
    (directory / 'PREPARATION_INCOMPLETE').write_text('Do not train on a partial dataset.\n')
    for entry in selected:
        case = entry['case_id']
        cid = 'imagecas_' + case
        if mode == 'smoke-roi':
            old = ROOT / f'build/imagecas-multicase/case-{case}'
            source, label = old / 'source.nii.gz', old / 'reference.nii.gz'
            response = old / 'result/vesselness.nii.gz'
            previous = json.loads((old / 'result/run.json').read_text())
            for key, expected in CONFIG['frangi'].items():
                if previous[key] != expected:
                    raise ValueError('Cached enhancement has a different configuration: ' + key)
        else:
            source, label = args.data / entry['source_file'], args.data / entry['label_file']
            response = directory / f'.frangi_{case}.nii.gz'
            if sha256(source) != entry['source_sha256'] or sha256(label) != entry['label_sha256']:
                raise ValueError(f'Source integrity mismatch for {case}')
            enhance(source, response, args.enhancer)
        image, ct = load_volume(source)
        vi, vesselness = load_volume(response)
        li, labels = load_volume(label)
        check_grid(image, vi); check_grid(image, li)
        if not np.array_equal(labels, np.rint(labels)) or (labels < 0).any():
            raise ValueError('Labels must contain non-negative integer classes')
        if vesselness.min() < -1e-6 or vesselness.max() > 1.000001:
            raise ValueError('Frangi channel must be continuous values in [0,1]')
        if not (labels > 0).any():
            raise ValueError(f'No foreground label in case {case}')
        save_like(ct.astype(np.float32), image, directory / 'imagesTr' / f'{cid}_0000.nii.gz')
        save_like(np.clip(vesselness, 0, 1).astype(np.float32), image, directory / 'imagesTr' / f'{cid}_0001.nii.gz')
        save_like((labels > 0).astype(np.uint8), image, directory / 'labelsTr' / f'{cid}.nii.gz')
        records.append({'case_id': cid, 'source_case_id': case, 'shape_xyz': list(image.shape),
                        'spacing_xyz_mm': nib.affines.voxel_sizes(image.affine).tolist(),
                        'source_sha256': sha256(source), 'label_sha256': sha256(label),
                        'vesselness_sha256': sha256(response), 'vesselness_range': [float(vesselness.min()), float(vesselness.max())]})
        if mode == 'full-volume':
            response.unlink()  # Only the temporary image created above, never the source.
        print(f'Prepared {cid}: {image.shape}', flush=True)
    dataset = {'channel_names': CONFIG['channel_names'], 'labels': CONFIG['labels'],
               'numTraining': len(records), 'file_ending': '.nii.gz',
               'overwrite_image_reader_writer': 'NibabelIO'}
    (directory / 'dataset.json').write_text(json.dumps(dataset, indent=2)+'\n')
    split = fixed_split([r['case_id'] for r in records])
    (directory / 'splits_final.json').write_text(json.dumps(split, indent=2)+'\n')
    provenance = {'mode': mode, 'configuration': CONFIG, 'cases': records,
                  'all_current_cases_previously_used_for_traditional_development': all(
                      r['source_case_id'] in {'1','100','102','103','106','107','109','11','110','111','112'} for r in records),
                  'split_status': 'internal development split; not a clean external/independent test',
                  'patient_grouping': 'case_id; verify source IDs represent independent patients before reporting',
                  'labels_used_for_enhancement': False,
                  'reference_guided_roi': mode == 'smoke-roi'}
    (directory / 'preparation.json').write_text(json.dumps(provenance, indent=2)+'\n')
    (directory / 'PREPARATION_INCOMPLETE').unlink()
    print('Dataset ready:', directory)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['smoke-roi', 'full-volume'], required=True)
    parser.add_argument('--work', type=Path, default=HERE / 'work')
    parser.add_argument('--data', type=Path, default=ROOT / 'datasets/imagecas-multicase')
    parser.add_argument('--cases', help='Comma-separated source IDs; default all 11 available cases')
    parser.add_argument('--enhancer', type=Path, default=ROOT / 'build/cmake/experiments/nnunet_frangi/medvessel_enhance.exe')
    prepare(parser.parse_args())
