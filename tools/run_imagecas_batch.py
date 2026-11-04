"""Fixed-parameter, reference-assisted local ROI evaluation for downloaded cases."""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PARAMETERS = ['--scales', '0.5,1,1.5,2', '--c', '100', '--low', '0.08', '--high', '0.2',
              '--min-intensity', '100', '--max-intensity', '900', '--min-component', '20', '--prune-mm', '1']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--watch-download', action='store_true')
    args = parser.parse_args()
    os.chdir(ROOT)
    data = ROOT / 'datasets/imagecas-multicase'
    output = ROOT / 'build/imagecas-multicase'
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['PATH'] = 'C:/msys64/ucrt64/bin;' + env['PATH']
    env['OMP_NUM_THREADS'] = '4'
    env['MPLCONFIGDIR'] = str(ROOT/'build/matplotlib')
    protocol = {'parameters': PARAMETERS, 'roi_half_width_mm': 20, 'threads': 4,
                'scope': 'reference-guided ROI and seed; local development evaluation, not full-volume or held-out benchmark',
                'original_development_case': '1', 'parameter_tuning_on_added_cases': False}
    (output/'protocol.json').write_text(json.dumps(protocol, indent=2)+'\n')
    rows = []
    processed = set()

    def run(name, arguments, log):
        executable = ROOT/'build/cmake'/f'medvessel_{name}.exe'
        result = subprocess.run([str(executable), *map(str, arguments)], env=env, stdout=log, stderr=log, timeout=900)
        if result.returncode:
            raise RuntimeError(f'{name} exited with code {result.returncode}')

    while True:
        manifest = json.loads((data/'manifest.json').read_text())
        for entry in manifest['cases']:
            case = entry['case_id']
            if case in processed:
                continue
            processed.add(case)
            directory = output/f'case-{case}'
            directory.mkdir(exist_ok=True)
            row = {'case_id': case, 'status': 'failed', 'previously_used_for_development': case == '1'}
            print(f'Processing case {case}', flush=True)
            try:
                with (directory/'process.log').open('w', encoding='utf-8') as log:
                    run('prepare_case', [data/entry['source_file'], data/entry['label_file'], directory, '20'], log)
                    roi = json.loads((directory/'roi.json').read_text())
                    seed = ','.join(map(str, roi['seed_voxel']))
                    run('cli', [directory/'source.nii.gz', directory/'result', *PARAMETERS, '--seed', seed], log)
                    run('evaluate', [directory/'result/mask.nii.gz', directory/'reference.nii.gz', directory/'result/metrics.json'], log)
                    metrics = json.loads((directory/'result/metrics.json').read_text())
                    row.update({k: metrics[k] for k in ['dice', 'cldice', 'hd95_mm', 'prediction_components', 'reference_components']})
                    row['status'] = 'ok'
                    row['roi_size'] = roi['source_roi_voxel'][3:]
                    plots = directory/'plots'
                    for name, path in [('source', directory/'source.nii.gz'), ('reference', directory/'reference.nii.gz'),
                                       ('vesselness', directory/'result/vesselness.nii.gz'), ('mask', directory/'result/mask.nii.gz')]:
                        run('export', [path, plots/name], log)
                    result = subprocess.run(['C:/Users/1/anaconda3/python.exe', 'tools/plot_reference.py', str(plots),
                        str(directory/'comparison.png'), '--slice', str(roi['seed_voxel'][2]), '--reference-mask-positive',
                        '--title', f'ImageCAS case {case} - ImageCAS-X v2 reference',
                        '--note', 'Fixed parameters; reference-guided local ROI and seed; not held-out benchmark',
                        '--reference-label', 'ImageCAS-X v2'], env=env, stdout=log, stderr=log, timeout=120)
                    if result.returncode:
                        row['plot_error'] = 'comparison plot failed; see process.log'
            except Exception as error:
                row['error'] = str(error)
            rows.append(row)
            successful_new = [r['dice'] for r in rows if r['status']=='ok' and not r['previously_used_for_development']]
            summary = {'protocol': protocol, 'download_complete': manifest['complete'], 'evaluated_cases': rows,
                       'additional_successes': len(successful_new),
                       'additional_mean_dice': sum(successful_new)/len(successful_new) if successful_new else None,
                       'failed_cases': [r['case_id'] for r in rows if r['status']!='ok']}
            partial = output/'summary.partial'
            partial.write_text(json.dumps(summary, indent=2)+'\n')
            os.replace(partial, output/'summary.json')
            fields = ['case_id', 'status', 'previously_used_for_development', 'dice', 'cldice', 'hd95_mm', 'error']
            with (output/'summary.csv').open('w', newline='', encoding='utf-8-sig') as f:
                writer = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
                writer.writeheader()
                writer.writerows(rows)
            print(f'Case {case}: {row}', flush=True)
        if manifest['complete'] or not args.watch_download:
            break
        time.sleep(5)
    print('BATCH EVALUATION COMPLETE', flush=True)


if __name__ == '__main__':
    main()
