"""Explicitly queue CUDA training after the full-volume validation succeeds."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import time

from workflow import HERE, dataset_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--dataset', type=int, default=511)
    parser.add_argument('--gpu-memory-gb', type=float, default=8.)
    parser.add_argument('--wait-seconds', type=int, default=14400)
    parser.add_argument('--execute-training', action='store_true', required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    lock = args.work / 'TRAINING_QUEUE_LOCK'
    with lock.open('x') as stream:
        stream.write('One training queue per work directory.\n')
    status = args.work / 'training_status.json'
    def record(stage, **details):
        status.write_text(json.dumps({'updated_utc': datetime.now(timezone.utc).isoformat(),
                                     'stage': stage, **details}, indent=2) + '\n')
    try:
        record('waiting_for_full_validation', training_started=False)
        deadline = time.monotonic() + args.wait_seconds
        while not (args.work / 'full_validation.json').is_file():
            if time.monotonic() >= deadline:
                raise TimeoutError('Full-volume validation did not finish within the queue timeout')
            time.sleep(10)
        report = json.loads((args.work / 'full_validation.json').read_text())
        dataset, provenance = dataset_path(args.work, args.dataset)
        if report['dataset'] != dataset.name or (args.work / 'FULL_VALIDATION_INCOMPLETE').exists():
            raise ValueError('Full-volume validation is incomplete or refers to another dataset')
        if provenance['mode'] != 'full-volume' or provenance['reference_guided_roi'] or provenance.get('data_kind') == 'synthetic':
            raise ValueError('Only real full-volume datasets may enter this training queue')
        if list((args.work / 'nnUNet_results' / dataset.name).rglob('*.pth')):
            raise FileExistsError('Existing checkpoints found; choose an explicit resume procedure')
        import torch
        if not torch.cuda.is_available():
            raise RuntimeError('CUDA PyTorch is required; refusing CPU training')
        # Test both a real CUDA kernel and a backward pass before a long training run.
        x = torch.randn((16, 16), device='cuda', requires_grad=True)
        (x @ x.T).square().mean().backward()
        torch.cuda.synchronize()
        if not torch.isfinite(x.grad).all():
            raise ValueError('CUDA backward check produced nonfinite values')
        del x
        torch.cuda.empty_cache()
        stages = [('cuda_environment', ['check']),
                  ('training_plan', ['plan', '--dataset', str(args.dataset), '--gpu-memory-gb', str(args.gpu_memory_gb)]),
                  ('cuda_forward', ['forward', '--dataset', str(args.dataset), '--device', 'cuda']),
                  ('training', ['train', '--dataset', str(args.dataset), '--device', 'cuda', '--execute-training'])]
        for stage, options in stages:
            record(stage, training_started=stage == 'training')
            with (args.work / f'{stage}.log').open('w', encoding='utf-8') as stream:
                result = subprocess.run([sys.executable, str(HERE / 'workflow.py'), *options,
                                         '--work', str(args.work)], stdout=stream, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f'{stage} failed; see {stage}.log')
        record('completed', training_started=True)
    except Exception as error:
        record('failed', error=str(error))
        raise
    finally:
        lock.unlink()


if __name__ == '__main__':
    main()
