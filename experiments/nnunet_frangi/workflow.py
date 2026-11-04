"""Explicit nnU-Net preparation/validation commands; training is opt-in and never implicit."""
import argparse
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent


def environment(work):
    env = os.environ.copy()
    for name in ('nnUNet_raw', 'nnUNet_preprocessed', 'nnUNet_results'):
        directory = work.resolve() / name
        directory.mkdir(parents=True, exist_ok=True)
        env[name] = str(directory)
        os.environ[name] = str(directory)
    env.setdefault('nnUNet_n_proc_DA', '2')
    env['nnUNet_compile'] = 'false'  # Portable eager-mode baseline, including native Windows.
    env.setdefault('OMP_NUM_THREADS', '4')
    return env


def dataset_path(work, dataset_id):
    matches = list((work / 'nnUNet_raw').glob(f'Dataset{dataset_id:03d}_*'))
    if len(matches) != 1:
        raise ValueError('Expected exactly one prepared dataset for ID ' + str(dataset_id))
    directory = matches[0]
    if (directory / 'PREPARATION_INCOMPLETE').exists():
        raise ValueError('Dataset preparation is incomplete')
    provenance = json.loads((directory / 'preparation.json').read_text())
    return directory, provenance


def command_path(name):
    executable = Path(sys.executable).parent / (name + ('.exe' if os.name == 'nt' else ''))
    if executable.exists():
        return str(executable)
    found = shutil.which(name)
    if not found:
        raise RuntimeError('Missing nnU-Net entry point: ' + name)
    return found


def check(args):
    import torch
    packages = {}
    for package in ('nnunetv2', 'dynamic-network-architectures', 'nibabel', 'numpy', 'scipy', 'scikit-image'):
        packages[package] = importlib.metadata.version(package)
    report = {'python': sys.version, 'packages': packages, 'torch': torch.__version__,
              'torch_cuda': torch.version.cuda, 'cuda_available': torch.cuda.is_available(),
              'training_started': False}
    if torch.cuda.is_available():
        report['gpu'] = torch.cuda.get_device_name(0)
        report['vram_gib'] = torch.cuda.get_device_properties(0).total_memory / 1024**3
        # Actually execute a CUDA kernel: device discovery alone does not prove compatibility.
        with torch.inference_mode():
            x = torch.ones((4, 4), device='cuda')
            report['cuda_kernel_ok'] = float((x @ x).sum().cpu()) == 64.
    (args.work / 'environment.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


def forward(args, dataset):
    import torch
    import numpy as np
    from nnunetv2.utilities.plans_handling.plans_handler import PlansManager
    from nnunetv2.utilities.get_network_from_plans import get_network_from_plans
    plans_file = args.work / 'nnUNet_preprocessed' / dataset.name / 'nnUNetPlans.json'
    plans = PlansManager(str(plans_file))
    configuration = plans.get_configuration('3d_fullres')
    ds = json.loads((dataset / 'dataset.json').read_text())
    torch.manual_seed(0)
    network = get_network_from_plans(configuration.network_arch_class_name,
                                    configuration.network_arch_init_kwargs,
                                    configuration.network_arch_init_kwargs_req_import,
                                    len(ds['channel_names']), 2,
                                    allow_init=True, deep_supervision=False)
    device = torch.device(args.device)
    if device.type == 'cuda':
        torch.cuda.reset_peak_memory_stats()
    network.to(device).eval()
    # Same planned architecture, small divisible patch: not a training memory benchmark.
    strides = configuration.network_arch_init_kwargs['strides']
    divisibility = np.prod(np.asarray(strides), axis=0)
    patch = [int(max(32, d * 2)) for d in divisibility]
    patch = [int(np.ceil(p / d) * d) for p, d in zip(patch, divisibility)]
    from nnunetv2.training.dataloading.nnunet_dataset import infer_dataset_class
    preprocessed = args.work / 'nnUNet_preprocessed' / dataset.name / configuration.data_identifier
    loader = infer_dataset_class(str(preprocessed))(str(preprocessed))
    case_id = sorted(loader.identifiers)[0]
    volume, _, _, _ = loader.load_case(case_id)
    starts = [max(0, (s - p) // 2) for s, p in zip(volume.shape[1:], patch)]
    block = np.asarray(volume[(slice(None), *[slice(s, s+p) for s, p in zip(starts, patch)])], dtype=np.float32)
    padding = [(0, 0)] + [(0, p - s) for p, s in zip(patch, block.shape[1:])]
    block = np.pad(block, padding)
    start = time.perf_counter()
    with torch.inference_mode():
        tensor = torch.from_numpy(np.ascontiguousarray(block[None])).to(device)
        logits = network(tensor)
        if not torch.isfinite(logits).all():
            raise ValueError('Nonfinite forward output')
        shape = list(logits.shape)
        if shape != [1, 2, *patch]:
            raise ValueError('Unexpected output shape')
    if device.type == 'cuda':
        torch.cuda.synchronize()
    report = {'purpose': 'Untrained architecture forward smoke check only; no segmentation accuracy',
              'dataset': dataset.name, 'device': str(device), 'input_shape': list(tensor.shape),
              'input_case': case_id, 'input_kind': 'center patch of real officially preprocessed dual-channel CTA',
              'output_shape': shape, 'planned_training_patch': configuration.patch_size,
              'parameters': sum(p.numel() for p in network.parameters()),
              'seconds': time.perf_counter() - start, 'finite': True,
              'training_started': False, 'optimizer_created': False, 'weights_saved': False}
    if device.type == 'cuda':
        report['peak_allocated_vram_mib'] = torch.cuda.max_memory_allocated() / 1024**2
    (args.work / 'forward_check.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['check', 'verify', 'plan', 'forward', 'train'])
    parser.add_argument('--work', type=Path, default=HERE / 'work')
    parser.add_argument('--dataset', type=int, default=512)
    parser.add_argument('--gpu-memory-gb', type=float, default=3.)
    parser.add_argument('--processes', type=int, default=1)
    parser.add_argument('--device', choices=['cpu', 'cuda'], default='cuda')
    parser.add_argument('--execute-training', action='store_true', help='Only relevant to explicit train stage')
    args = parser.parse_args()
    env = environment(args.work)
    if args.stage == 'check':
        check(args); return
    dataset, provenance = dataset_path(args.work, args.dataset)
    if args.stage == 'verify':
        from nnunetv2.experiment_planning.verify_dataset_integrity import verify_dataset_integrity
        verify_dataset_integrity(str(dataset), num_processes=args.processes)
        (args.work / f'verified_{args.dataset}.json').write_text(json.dumps({'dataset': dataset.name,
            'case_count': len(provenance['cases']), 'training_started': False}, indent=2)+'\n')
    elif args.stage == 'plan':
        command = [command_path('nnUNetv2_plan_and_preprocess'), '-d', str(args.dataset),
                   '--verify_dataset_integrity', '-c', '3d_fullres', '-np', str(args.processes),
                   '-npfp', str(args.processes),
                   '-gpu_memory_target', str(args.gpu_memory_gb)]
        print(subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, check=True, env=env)
        # Also support pinned versions which do not copy a raw dataset's split automatically.
        shutil.copy2(dataset / 'splits_final.json', args.work / 'nnUNet_preprocessed' / dataset.name / 'splits_final.json')
    elif args.stage == 'forward':
        forward(args, dataset)
    else:
        if provenance['mode'] != 'full-volume' or provenance['reference_guided_roi']:
            raise ValueError('Training on reference-guided smoke ROIs is deliberately disabled; prepare full-volume data')
        if not (args.work / 'nnUNet_preprocessed' / dataset.name / 'nnUNetPlans.json').exists():
            raise ValueError('Run full-volume planning first')
        command = [command_path('nnUNetv2_train'), str(args.dataset), '3d_fullres', '0', '--npz', '-device', args.device]
        print(subprocess.list2cmdline(command), flush=True)
        if args.execute_training:
            subprocess.run(command, check=True, env=env)
        else:
            print('Command preview only. No training started. Add --execute-training only when ready.')


if __name__ == '__main__':
    main()
