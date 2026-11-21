"""Preprocess with frozen plans; verify every expected cache before completion."""
import json
from pathlib import Path
import subprocess
from workflow import environment, command_path
from persistent_queue import save

HERE = Path(__file__).resolve().parent


def main():
    work = HERE / 'work/long-run/learning-curve-140'
    manifest = json.loads((work / 'manifest.json').read_text())
    env = environment(work)
    subprocess.run([command_path('nnUNetv2_preprocess'), '-d', '511', '-c', '3d_fullres', '-np', '2'],
                   env=env, check=True)
    cache = work / 'nnUNet_preprocessed/Dataset511_CoronaryFrangi/nnUNetPlans_3d_fullres'
    records = []
    from nnunetv2.training.dataloading.nnunet_dataset import infer_dataset_class
    dataset = infer_dataset_class(str(cache))(str(cache))
    for case in manifest['train'] + manifest['validation']:
        data, seg, _, properties = dataset.load_case(case)
        if data.shape[0] != 1 or data.shape[1:] != seg.shape[1:]:
            raise ValueError('Unexpected preprocessed channel/label shape for ' + case)
        records.append(dict(case=case, shape=list(data.shape)))
    save(work / 'preprocessing_completed.json', dict(cases=records, frozen_plans=True,
         patient_independence_verified=False, training_started=False))


if __name__ == '__main__':
    main()
