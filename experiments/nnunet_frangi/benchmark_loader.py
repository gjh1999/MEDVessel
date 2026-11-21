"""Measure the real augmentation pipeline without training or changing checkpoints."""
import argparse
import json
import os
from pathlib import Path
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--workers', type=int, required=True)
    parser.add_argument('--batches', type=int, default=100)
    args = parser.parse_args()
    from workflow import HERE, environment
    work = HERE / 'work/full-real-100'
    os.environ.update(environment(work))
    os.environ['nnUNet_results'] = str(work / 'loader_benchmark/models')
    os.environ['nnUNet_n_proc_DA'] = str(args.workers)
    import torch
    from nnunetv2.run.run_training import get_trainer_from_args
    torch.set_num_threads(1)
    trainer = get_trainer_from_args('511', '3d_fullres', 0, device=torch.device('cpu'))
    trainer.batch_size = trainer.configuration_manager.batch_size
    train = val = None
    try:
        train, val = trainer.get_dataloaders()
        for _ in range(12):
            next(train)
        waits = []
        start = time.perf_counter()
        for _ in range(args.batches):
            before = time.perf_counter()
            batch = next(train)
            waits.append(time.perf_counter() - before)
            del batch
        elapsed = time.perf_counter() - start
        report = dict(workers=args.workers, batches=args.batches, seconds=elapsed,
                      seconds_per_batch=elapsed / args.batches,
                      batches_per_second=args.batches / elapsed,
                      note='CPU-only real augmentation; concurrent training may contend for CPU and disk')
        target = work / f'loader_benchmark/workers_{args.workers}.json'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(report, indent=2))
        print(json.dumps(report), flush=True)
    finally:
        for loader in (train, val):
            if loader is not None and hasattr(loader, '_finish'):
                loader._finish()


if __name__ == '__main__':
    main()
