"""Run a genuinely trained dual-channel nnU-Net; never substitute random weights."""
import argparse
import json
from pathlib import Path
import torch
from workflow import HERE, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model_directory', type=Path, help='nnUNetTrainer__nnUNetPlans__3d_fullres directory')
    parser.add_argument('images', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--work', type=Path, default=HERE / 'work')
    parser.add_argument('--fold', type=int, default=0)
    parser.add_argument('--device', choices=['cuda', 'cpu'], default='cuda')
    args = parser.parse_args()
    environment(args.work)
    metadata = json.loads((args.model_directory / 'dataset.json').read_text())
    if metadata['channel_names'] != {'0': 'CT', '1': 'nonorm'}:
        raise ValueError('Expected the CT + Frangi dual-channel model')
    if not (args.model_directory / f'fold_{args.fold}/checkpoint_final.pth').exists():
        raise FileNotFoundError('No trained final checkpoint; complete training first')
    if args.output.exists():
        raise FileExistsError('Use a new output directory')
    from nnunetv2.inference.predict_from_raw_data import nnUNetPredictor
    predictor = nnUNetPredictor(tile_step_size=.5, use_gaussian=True, use_mirroring=False,
        perform_everything_on_device=False, device=torch.device(args.device), verbose=False,
        verbose_preprocessing=False, allow_tqdm=True)
    predictor.initialize_from_trained_model_folder(str(args.model_directory),
        use_folds=(args.fold,), checkpoint_name='checkpoint_final.pth')
    predictor.predict_from_files(str(args.images), str(args.output), save_probabilities=False,
        overwrite=False, num_processes_preprocessing=1, num_processes_segmentation_export=1)


if __name__ == '__main__':
    main()
