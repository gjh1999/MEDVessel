"""Isolated lower-LR continuation, queued behind the frozen baseline evaluation."""
import argparse
from datetime import datetime, timezone
import json
import msvcrt
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
BASE = HERE / 'work/full-real-100'
WORK = HERE / 'work/accuracy-finetune-01'


def learning_rate(epoch, start, budget=200, initial=0.001):
    return initial * max(0., 1. - max(0, epoch - start) / budget) ** 0.9


def record(stage, **details):
    WORK.mkdir(parents=True, exist_ok=True)
    target = WORK / 'status.json'
    temporary = target.with_suffix('.tmp')
    temporary.write_text(json.dumps(dict(stage=stage, updated_utc=datetime.now(timezone.utc).isoformat(),
                                         **details), indent=2))
    temporary.replace(target)


def train():
    from workflow import environment
    os.environ.update(environment(BASE))
    os.environ['nnUNet_results'] = str(WORK / 'nnUNet_results')
    os.environ['nnUNet_n_proc_DA'] = '6'
    import torch
    from nnunetv2.run.run_training import get_trainer_from_args
    from train_with_early_stopping import stopping_state
    if not torch.cuda.is_available():
        raise RuntimeError('CUDA required')
    trainer = get_trainer_from_args('511', '3d_fullres', 0, device=torch.device('cuda'))
    output = Path(trainer.output_folder)
    baseline = BASE / 'nnUNet_results' / output.parent.parent.name / output.parent.name / 'fold_0'
    config_path = WORK / 'experiment.json'
    if not config_path.exists():
        source = baseline / 'checkpoint_final.pth'
        checkpoint = torch.load(str(source), map_location='cpu', weights_only=False)
        config = dict(start_epoch=int(checkpoint['current_epoch']), budget=200, initial_lr=0.001,
                      patience=80, min_delta=0.001, source=str(source),
                      source_mtime_ns=source.stat().st_mtime_ns,
                      change='Lower LR only; same split, model, loss, batch size and augmentation')
        config_path.write_text(json.dumps(config, indent=2))
        del checkpoint
        trainer.load_checkpoint(str(source))
        shutil.copy2(baseline / 'checkpoint_best.pth', output / 'checkpoint_best.pth')
    else:
        config = json.loads(config_path.read_text())
        final = output / 'checkpoint_final.pth'
        candidates = [p for p in output.glob('checkpoint_*.pth') if p.name in
                      ('checkpoint_latest.pth', 'checkpoint_best.pth')]
        # The copied baseline best is older than the fine-tune start. If no local
        # periodic checkpoint exists yet, restart from the declared baseline final.
        source = final if final.exists() else (max(candidates, key=lambda p: p.stat().st_mtime_ns)
                    if (output / 'checkpoint_latest.pth').exists() else Path(config['source']))
        trainer.load_checkpoint(str(source))
    trainer.num_epochs = config['start_epoch'] + config['budget']
    trainer.save_every = 10

    class Scheduler:
        def step(self, epoch):
            lr = learning_rate(epoch, config['start_epoch'], config['budget'], config['initial_lr'])
            for group in trainer.optimizer.param_groups:
                group['lr'] = lr

    trainer.lr_scheduler = Scheduler()
    original_end = trainer.on_epoch_end

    class EarlyStop(Exception):
        pass

    def on_epoch_end():
        original_end()
        history = trainer.logger.my_fantastic_logging['ema_fg_dice'][config['start_epoch']:]
        state = stopping_state(history, config['patience'], config['min_delta'])
        (output / 'early_stopping.json').write_text(json.dumps(state, indent=2))
        record('training', epoch=trainer.current_epoch, lr=trainer.optimizer.param_groups[0]['lr'],
               early_stopping=state)
        if state['stop']:
            raise EarlyStop()

    trainer.on_epoch_end = on_epoch_end
    final_exists = (output / 'checkpoint_final.pth').exists()
    if not final_exists:
        record('training', epoch=trainer.current_epoch)
        try:
            trainer.run_training()
        except EarlyStop:
            trainer.on_train_end()
    if not (output / 'validation/summary.json').exists():
        record('full_volume_validation')
        trainer.perform_actual_validation(save_probabilities=True)
    record('completed', validation_summary=str(output / 'validation/summary.json'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-training', action='store_true', required=True)
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--preflight', action='store_true')
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    if args.preflight:
        import torch
        source = next((BASE / 'nnUNet_results').rglob('checkpoint_final.pth'))
        checkpoint = torch.load(str(source), map_location='cpu', weights_only=False)
        assert checkpoint['current_epoch'] == len(checkpoint['logging']['ema_fg_dice'])
        assert checkpoint['optimizer_state']['param_groups']
        assert all(torch.isfinite(t).all() for t in checkpoint['network_weights'].values()
                   if torch.is_floating_point(t))
        print(json.dumps(dict(source=str(source), epoch=checkpoint['current_epoch'],
                              old_lr=checkpoint['optimizer_state']['param_groups'][0]['lr'],
                              new_lr=learning_rate(checkpoint['current_epoch'], checkpoint['current_epoch']),
                              checkpoint_finite=True)), flush=True)
        return
    if args.worker:
        train()
        return
    with (WORK / 'queue.lock').open('a+b') as lock:
        lock.seek(0)
        if not lock.read(1):
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        while True:
            if (WORK / 'STOP').exists():
                record('stopped')
                return
            state = json.loads((BASE / 'guardian_status.json').read_text())
            if state['stage'] == 'completed':
                break
            record('waiting_for_baseline_evaluation', baseline_stage=state['stage'])
            time.sleep(15)
        attempt = 0
        while not (WORK / 'STOP').exists():
            attempt += 1
            with (WORK / 'training.log').open('a', encoding='utf-8') as log:
                child = subprocess.Popen([sys.executable, '-u', __file__, '--execute-training', '--worker'],
                                         stdout=log, stderr=subprocess.STDOUT)
                while child.poll() is None:
                    if (WORK / 'STOP').exists():
                        subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
                        child.wait()
                        record('stopped')
                        return
                    time.sleep(5)
            if child.returncode == 0:
                return
            record('retry_wait', attempt=attempt, exit_code=child.returncode)
            for _ in range(min(300, 30 * attempt)):
                if (WORK / 'STOP').exists():
                    record('stopped')
                    return
                time.sleep(1)


if __name__ == '__main__':
    main()
