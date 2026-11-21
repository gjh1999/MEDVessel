"""Queue paired CT-only/dual-channel experiments without reading holdout volumes."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
BASE = HERE / 'work/full-real-100'
WORK = HERE / 'work/preprocessing-ablation'


def record(stage, **details):
    WORK.mkdir(parents=True, exist_ok=True)
    path = WORK / 'status.json'
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(dict(stage=stage, updated_utc=datetime.now(timezone.utc).isoformat(), **details), indent=2))
    temp.replace(path)


def prepare(arm):
    from workflow import environment
    work = WORK / arm
    env = environment(work)
    name = 'Dataset511_CoronaryFrangi'
    source = BASE / 'nnUNet_raw' / name
    split = json.loads((source / 'holdout.json').read_text())
    ids = split['train'] + split['val']
    assert len(ids) == 85 and len(set(ids)) == 85
    assert not set(ids) & set(split['test'])
    dataset = work / 'nnUNet_raw' / name
    for folder in ('imagesTr', 'labelsTr'):
        (dataset / folder).mkdir(parents=True, exist_ok=True)
    for cid in ids:
        names = [('labelsTr', f'{cid}.nii.gz')]
        names += [('imagesTr', f'{cid}_{channel:04d}.nii.gz') for channel in range(1 if arm == 'ct-only' else 2)]
        for folder, filename in names:
            dest = dataset / folder / filename
            if not dest.exists():
                os.link(source / folder / filename, dest)
    metadata = json.loads((source / 'dataset.json').read_text())
    metadata['numTraining'] = 85
    if arm == 'ct-only':
        metadata['channel_names'] = {'0': 'CT'}
    (dataset / 'dataset.json').write_text(json.dumps(metadata, indent=2))
    processed = work / 'nnUNet_preprocessed' / name
    processed.mkdir(parents=True, exist_ok=True)
    (processed / 'dataset.json').write_text(json.dumps(metadata, indent=2))
    original = BASE / 'nnUNet_preprocessed' / name
    plans = json.loads((original / 'nnUNetPlans.json').read_text())
    if arm == 'ct-only':
        plans['foreground_intensity_properties_per_channel'].pop('1', None)
        for config in plans['configurations'].values():
            for key in ('normalization_schemes', 'use_mask_for_norm'):
                if key in config:
                    config[key] = config[key][:1]
    (processed / 'nnUNetPlans.json').write_text(json.dumps(plans, indent=2))
    shutil.copy2(original / 'dataset_fingerprint.json', processed / 'dataset_fingerprint.json')
    (processed / 'splits_final.json').write_text(json.dumps([dict(train=split['train'], val=split['val'])]))
    env['nnUNet_n_proc_DA'] = '6'
    return work, env


def worker(arm):
    work, env = prepare(arm)
    from workflow import command_path
    record('preprocessing', arm=arm)
    subprocess.run([command_path('nnUNetv2_preprocess'), '-d', '511', '-c', '3d_fullres', '-np', '2'], env=env, check=True)
    os.environ.update(env)
    import random
    import numpy as np
    import torch
    random.seed(20261108)
    np.random.seed(20261108)
    torch.manual_seed(20261108)
    torch.cuda.manual_seed_all(20261108)
    from nnunetv2.run.run_training import get_trainer_from_args
    trainer = get_trainer_from_args('511', '3d_fullres', 0, device=torch.device('cuda'))
    trainer.num_epochs = 445
    trainer.save_every = 10
    output = Path(trainer.output_folder)
    final = output / 'checkpoint_final.pth'
    if not final.exists():
        latest = output / 'checkpoint_latest.pth'
        if latest.exists():
            trainer.load_checkpoint(str(latest))
        else:
            trainer.initialize()

        class Schedule:
            def step(self, epoch):
                lr = (.01 * (1 - epoch / 1000) ** .9 if epoch < 245
                      else .001 * max(0, 1 - (epoch - 245) / 200) ** .9)
                for group in trainer.optimizer.param_groups:
                    group['lr'] = lr

        trainer.lr_scheduler = Schedule()
        original_end = trainer.on_epoch_end

        def epoch_end():
            original_end()
            record('training', arm=arm, epoch=trainer.current_epoch, budget=445)

        trainer.on_epoch_end = epoch_end
        trainer.run_training()
    else:
        trainer.load_checkpoint(str(final))
    if not (output / 'validation/summary.json').exists():
        record('validation', arm=arm)
        trainer.perform_actual_validation(save_probabilities=True)
    record('arm_completed', arm=arm)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', choices=['ct-only', 'dual'])
    args = parser.parse_args()
    if args.worker:
        worker(args.worker)
        return
    import msvcrt
    WORK.mkdir(parents=True, exist_ok=True)
    with (WORK / 'queue.lock').open('a+b') as lock:
        lock.write(b'0')
        lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        while True:
            state = json.loads((HERE / 'work/accuracy-finetune-01/comparison_status.json').read_text())
            if state['stage'] == 'completed':
                break
            if state['stage'] == 'failed':
                raise RuntimeError('Comparison failed; resolve it before GPU training')
            record('waiting_for_comparison', comparison_stage=state['stage'])
            time.sleep(30)
        for arm in ('ct-only', 'dual'):
            with (WORK / f'{arm}.log').open('a', encoding='utf-8') as log:
                subprocess.run([sys.executable, '-u', __file__, '--worker', arm], stdout=log, stderr=subprocess.STDOUT, check=True)
        record('paired_training_completed', next_step='Unified validation scoring before window experiments')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        record('failed', error=str(error))
        raise
