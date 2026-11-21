"""Run frozen CT screening validation against an immutable prefix checkpoint."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from prepare_dataset import sha256
from persistent_queue import save
from workflow import environment

HERE = Path(__file__).resolve().parent
BASE = HERE / 'work/full-real-100'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--prefix', type=int, default=50)
    args = parser.parse_args()
    work = args.work.resolve()
    prefix = json.loads((work / f'prefix_{args.prefix}.json').read_text())
    if prefix['completed_epochs'] != args.prefix:
        raise ValueError('Wrong training prefix')
    selection = json.loads((HERE / 'work/long-run/screening_validation.json').read_text())
    cases = selection['cases']
    frozen = json.loads((HERE / 'work/long-run/frozen_splits.json').read_text())
    assert set(cases).issubset(frozen['validation_historical'])
    assert not set(cases) & set(frozen['test_sealed'])
    output = work / f'evaluation_{args.prefix}'
    output.mkdir(exist_ok=True)
    checkpoint = Path(prefix['checkpoint'])
    signature = dict(checkpoint_sha256=sha256(checkpoint), cases=cases, mirroring=False,
                     prefix=args.prefix, validation_only=True)
    receipt = output / 'manifest.json'
    if receipt.exists() and json.loads(receipt.read_text()) != signature:
        raise ValueError('Existing evaluation signature differs')
    save(receipt, signature)
    inputs = output / 'images'
    inputs.mkdir(exist_ok=True)
    for case in cases:
        source = BASE / 'validation_images' / (case + '_0000.nii.gz')
        target = inputs / source.name
        if not target.exists():
            os.link(source, target)
    model = checkpoint.parent.parent
    snapshot = checkpoint.parent / f'checkpoint_prefix_{args.prefix}.pth'
    if not snapshot.exists():
        shutil.copy2(checkpoint, snapshot)
    if sha256(snapshot) != signature['checkpoint_sha256']:
        raise ValueError('Prefix snapshot mismatch')
    os.environ.update(environment(work))
    import torch
    from nnunetv2.inference.predict_from_raw_data import nnUNetPredictor
    from predict import restore_prediction_units
    predictor = nnUNetPredictor(tile_step_size=.5, use_gaussian=True,
        use_mirroring=False, perform_everything_on_device=False,
        device=torch.device('cuda'), verbose=False, verbose_preprocessing=False)
    predictor.initialize_from_trained_model_folder(str(model), use_folds=(0,),
        checkpoint_name=snapshot.name)
    predictions = output / 'predictions'
    predictor.predict_from_files(str(inputs), str(predictions), save_probabilities=False,
        overwrite=False, num_processes_preprocessing=1, num_processes_segmentation_export=1)
    restore_prediction_units(inputs, predictions)
    case_list = output / 'cases.json'
    save(case_list, cases)
    subprocess.run([sys.executable, str(HERE / 'evaluate_predictions.py'), str(predictions),
        str(BASE / 'nnUNet_raw/Dataset511_CoronaryFrangi/labelsTr'),
        str(output / 'metrics.json'), '--case-list', str(case_list)], check=True)
    save(output / 'completed.json', signature)


if __name__ == '__main__':
    main()
