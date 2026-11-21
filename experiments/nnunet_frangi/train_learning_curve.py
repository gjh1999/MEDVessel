"""Train a frozen CT subset to a predefined prefix of the 445-epoch schedule."""
import argparse
import json
import os
from pathlib import Path
import random
import sys
from persistent_queue import save
from workflow import environment

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--prefix', type=int, choices=(50, 100, 200, 445), default=50)
    args = parser.parse_args()
    work = args.work.resolve()
    manifest = json.loads((work / 'manifest.json').read_text())
    os.environ.update(environment(work))
    os.environ['nnUNet_n_proc_DA'] = '6'
    import numpy as np
    import torch
    from nnunetv2.run.run_training import get_trainer_from_args
    random.seed(manifest['seed'])
    np.random.seed(manifest['seed'])
    torch.manual_seed(manifest['seed'])
    torch.cuda.manual_seed_all(manifest['seed'])
    trainer = get_trainer_from_args('511', '3d_fullres', 0, device=torch.device('cuda'))
    trainer.num_epochs = 445
    trainer.save_every = 10
    output = Path(trainer.output_folder)
    checkpoint = output / 'checkpoint_latest.pth'
    rng_restored = False
    if checkpoint.exists():
        trainer.load_checkpoint(str(checkpoint))
        rng_path = output / 'rng_state.pth'
        if rng_path.exists():
            rng = torch.load(rng_path, weights_only=False, map_location='cpu')
            if rng.get('completed_epochs') == trainer.current_epoch:
                random.setstate(rng['python'])
                np.random.set_state(rng['numpy'])
                torch.set_rng_state(rng['torch'])
                torch.cuda.set_rng_state_all(rng['cuda'])
                rng_restored = True
            else:
                print('RNG sidecar epoch mismatch; using seeded main process state', flush=True)
    else:
        trainer.initialize()

    class Schedule:
        def step(self, epoch):
            lr = .01 * (1-epoch/1000)**.9 if epoch < 245 else .001 * max(0, 1-(epoch-245)/200)**.9
            for group in trainer.optimizer.param_groups:
                group['lr'] = lr

    trainer.lr_scheduler = Schedule()
    # Augmentation worker RNG streams restart; resumption is not bitwise deterministic.
    save(work / 'resume_policy.json', dict(model_optimizer_scaler_restored=checkpoint.exists(),
         main_rng_restored=rng_restored,
         augmentation_workers_restart=True, schedule_endpoint=445))
    original = trainer.on_epoch_end

    def epoch_end():
        completed = trainer.current_epoch + 1
        stop = Path(os.environ.get('LONG_RUN_STOP', str(work / 'STOP'))).exists()
        boundary = completed >= args.prefix
        if stop or boundary or completed % trainer.save_every == 0:
            trainer.save_checkpoint(str(checkpoint))
            # Trainer checkpoint uses current_epoch+1; save before on_epoch_end increments.
            torch.save(dict(completed_epochs=completed, python=random.getstate(), numpy=np.random.get_state(),
                            torch=torch.get_rng_state(), cuda=torch.cuda.get_rng_state_all()),
                       output / 'rng_state.pth')
        original()
        save(work / 'training_status.json', dict(status='paused' if stop else 'prefix_completed' if boundary else 'running',
             completed_epochs=completed, prefix=args.prefix, total_schedule_epochs=445))
        if stop:
            raise SystemExit(75)
        if boundary and args.prefix < 445:
            save(work / f'prefix_{args.prefix}.json', dict(completed_epochs=completed,
                 checkpoint=str(checkpoint), full_volume_evaluation_pending=True))
            raise SystemExit(0)

    trainer.on_epoch_end = epoch_end
    if trainer.current_epoch >= args.prefix:
        raise RuntimeError('Requested prefix already reached; verify outputs before reusing')
    trainer.run_training()


if __name__ == '__main__':
    main()
