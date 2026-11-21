"""Run a genuinely trained dual-channel nnU-Net; never substitute random weights."""
import argparse
import json
from pathlib import Path
import torch
from workflow import HERE, environment


def restore_prediction_units(images, output):
    import nibabel as nib
    import numpy as np
    from prepare_dataset import check_grid
    for source in images.glob('*_0000.nii.gz'):
        target = output / (source.name[:-12] + '.nii.gz')
        reference = nib.load(source)
        if (len(reference.shape) != 3 or reference.header.get_xyzt_units()[0] != 'mm'
                or not np.isfinite(reference.affine).all()):
            raise ValueError(f'Invalid source CT geometry or units: {source}')
        prediction = nib.load(target)
        check_grid(prediction, reference)
        units = prediction.header.get_xyzt_units()[0]
        if units not in ('unknown', 'mm'):
            raise ValueError(f'Unexpected prediction units: {target}: {units}')
        if units == 'unknown':
            data = np.asanyarray(prediction.dataobj).copy()
            header = prediction.header.copy()
            header.set_xyzt_units('mm')
            repaired = nib.Nifti1Image(data, prediction.affine, header)
            temporary = target.with_name(target.name[:-7] + '.units-tmp.nii.gz')
            nib.save(repaired, temporary)
            temporary.replace(target)


def validate_channels(directory):
    if (directory / 'PREPARATION_INCOMPLETE').exists():
        raise ValueError('Inference preparation is incomplete')
    channels = [set(), set()]
    for path in directory.glob('*.nii.gz'):
        for index in (0, 1):
            suffix = f'_{index:04d}.nii.gz'
            if path.name.endswith(suffix):
                channels[index].add(path.name[:-len(suffix)])
                break
        else:
            raise ValueError(f'Unexpected inference image filename: {path.name}')
    if not channels[0] or channels[0] != channels[1]:
        raise ValueError('Each inference case requires both CT and Frangi channels')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model_directory', type=Path, help='nnUNetTrainer__nnUNetPlans__3d_fullres directory')
    parser.add_argument('images', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--work', type=Path, default=HERE / 'work')
    parser.add_argument('--fold', type=int, default=0)
    parser.add_argument('--checkpoint', default='checkpoint_final.pth', choices=['checkpoint_final.pth', 'checkpoint_best.pth'])
    parser.add_argument('--mirroring', action='store_true')
    parser.add_argument('--resume', action='store_true', help='Resume matching recorded inference only')
    parser.add_argument('--device', choices=['cuda', 'cpu'], default='cuda')
    args = parser.parse_args()
    validate_channels(args.images)
    environment(args.work)
    metadata = json.loads((args.model_directory / 'dataset.json').read_text())
    if metadata['channel_names'] != {'0': 'CT', '1': 'nonorm'}:
        raise ValueError('Expected the CT + Frangi dual-channel model')
    if not (args.model_directory / f'fold_{args.fold}' / args.checkpoint).exists():
        raise FileNotFoundError('No trained final checkpoint; complete training first')
    manifest = {'model': str(args.model_directory.resolve()), 'images': str(args.images.resolve()),
                'checkpoint': args.checkpoint, 'fold': args.fold, 'mirroring': args.mirroring,
                'checkpoint_mtime_ns': (args.model_directory / f'fold_{args.fold}' / args.checkpoint).stat().st_mtime_ns}
    receipt = args.output / 'inference_manifest.json'
    if args.output.exists() and not args.resume:
        raise FileExistsError('Use a new output directory')
    if args.output.exists() and (not receipt.exists() or json.loads(receipt.read_text()) != manifest):
        raise ValueError('Existing predictions lack a matching inference manifest')
    args.output.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(manifest, indent=2))
    from nnunetv2.inference.predict_from_raw_data import nnUNetPredictor
    predictor = nnUNetPredictor(tile_step_size=.5, use_gaussian=True, use_mirroring=args.mirroring,
        perform_everything_on_device=False, device=torch.device(args.device), verbose=False,
        verbose_preprocessing=False, allow_tqdm=True)
    predictor.initialize_from_trained_model_folder(str(args.model_directory),
        use_folds=(args.fold,), checkpoint_name=args.checkpoint)
    predictor.predict_from_files(str(args.images), str(args.output), save_probabilities=False,
        overwrite=False, num_processes_preprocessing=1, num_processes_segmentation_export=1)
    restore_prediction_units(args.images, args.output)


if __name__ == '__main__':
    main()
