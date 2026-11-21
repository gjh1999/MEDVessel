"""Apply resume-safe early stopping without changing nnU-Net's LR schedule."""
import json
import math
import os
from pathlib import Path

PATIENCE = 50
MIN_DELTA = 0.002


def stopping_state(history, patience=PATIENCE, min_delta=MIN_DELTA):
    anchor = None
    last_improvement = 0
    for epoch, value in enumerate(history):
        value = float(value)
        if not math.isfinite(value):
            raise ValueError('Nonfinite EMA Dice; cannot assess early stopping')
        if anchor is None or value >= anchor + min_delta:
            anchor, last_improvement = value, epoch
    stale = len(history) - 1 - last_improvement if history else 0
    return {'patience': patience, 'min_delta': min_delta, 'metric': 'ema_fg_dice',
            'epochs_observed': len(history), 'epochs_without_significant_improvement': stale,
            'anchor': anchor, 'stop': bool(history) and stale >= patience}


def main():
    from nnunetv2.training.nnUNetTrainer.nnUNetTrainer import nnUNetTrainer
    from nnunetv2.run.run_training import run_training_entry

    class EarlyStop(Exception):
        pass

    original_end = nnUNetTrainer.on_epoch_end
    original_run = nnUNetTrainer.run_training
    original_load = nnUNetTrainer.load_checkpoint

    def load_checkpoint(self, checkpoint):
        # A best checkpoint may contain more recent optimizer/history state than
        # the periodically saved latest checkpoint. Preserve the most progress.
        if (os.environ.get('MEDVESSEL_NEWEST_CHECKPOINT') == '1'
                and isinstance(checkpoint, str)
                and Path(checkpoint).name == 'checkpoint_latest.pth'):
            candidates = [p for p in Path(checkpoint).parent.glob('checkpoint_*.pth')
                          if p.name in ('checkpoint_latest.pth', 'checkpoint_best.pth')]
            checkpoint = str(max(candidates, key=lambda p: p.stat().st_mtime_ns))
            self.print_to_log_file('Resume from most recently saved checkpoint:', checkpoint)
        return original_load(self, checkpoint)

    def on_epoch_end(self):
        original_end(self)
        state = stopping_state(self.logger.my_fantastic_logging['ema_fg_dice'])
        target = Path(self.output_folder) / 'early_stopping.json'
        target.write_text(json.dumps(state, indent=2))
        if state['stop']:
            self.print_to_log_file('Early stopping:', state)
            raise EarlyStop()

    def run_training(self):
        try:
            original_run(self)
        except EarlyStop:
            # Preserve normal final checkpoint/export and cleanup. The run entry
            # then performs its standard full-volume validation.
            self.on_train_end()

    nnUNetTrainer.on_epoch_end = on_epoch_end
    nnUNetTrainer.run_training = run_training
    nnUNetTrainer.load_checkpoint = load_checkpoint
    run_training_entry()


if __name__ == '__main__':
    main()
