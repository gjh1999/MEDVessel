"""Run the C++ two-stage mode and verify it against independent Python propagation on all development cases."""
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from scipy import ndimage
from plot_reference import read
from explore_segmentation import grow,score
from run_imagecas_batch import PARAMETERS

ROOT=Path(__file__).resolve().parents[1]


def main():
    baseline=json.loads((ROOT/'build/imagecas-multicase/summary.json').read_text())
    output=ROOT/'build/segmentation-optimized';output.mkdir(exist_ok=True)
    env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env['PATH'];env['OMP_NUM_THREADS']='4'
    protocol={'scope':'Retrospectively tuned development data, not held-out validation',
              'growth_band_mm':.5,'seed_core_ratio':.35,'fixed_parameters':PARAMETERS,
              'selection':'Choose balanced configuration that improves every development case, including case 1; no per-case parameter tuning',
              'sweeps':['build/segmentation-exploration/sweep.json','build/segmentation-exploration/bounded-sweep.json']}
    (output/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
    rows=[]
    for original in baseline['evaluated_cases']:
        case=original['case_id'];data=ROOT/f'build/imagecas-multicase/case-{case}';destination=output/f'case-{case}'
        destination.mkdir(exist_ok=True)
        roi=json.loads((data/'roi.json').read_text());seed=roi['seed_voxel']
        print('C++ core growth case',case,flush=True)
        with (destination/'process.log').open('w',encoding='utf-8') as log:
            def run(name,args):
                subprocess.run([str(ROOT/f'build/cmake/medvessel_{name}.exe'),*map(str,args)],env=env,stdout=log,stderr=log,check=True,timeout=900)
            run('cli',[data/'source.nii.gz',destination/'result',*PARAMETERS,'--seed',','.join(map(str,seed)),
                       '--growth-band-mm','.5','--seed-core-ratio','.35'])
            run('evaluate',[destination/'result/mask.nii.gz',data/'reference.nii.gz',destination/'result/metrics.json'])
            for name in ['mask','core']:
                run('export',[destination/f'result/{name}.nii.gz',destination/f'plots/{name}'])
        ct,geometry=read(data/'plots/source');response=read(data/'plots/vesselness')[0];reference=read(data/'plots/reference')[0]>0
        eligible=(ct>=100)&(ct<=900)&(response>=.08)
        threshold=float(np.float32(max(.08,.35*float(response[tuple(reversed(seed))]))))
        core=grow(eligible&(response>=threshold),seed,26)
        distance=ndimage.distance_transform_edt(~core,sampling=geometry['spacing_mm'][::-1]).astype(np.float32)
        radius=.5+4*np.finfo(np.float32).eps
        expected=grow(eligible&(distance<=radius),seed,26)
        actual=read(destination/'plots/mask')[0]>0;actual_core=read(destination/'plots/core')[0]>0
        if not np.array_equal(core,actual_core) or not np.array_equal(expected,actual):
            raise AssertionError(f'Python/C++ disagreement case {case}; core={np.count_nonzero(core!=actual_core)}, mask={np.count_nonzero(expected!=actual)}')
        metric=score(actual,reference);measured=json.loads((destination/'result/metrics.json').read_text())
        if abs(metric['dice']-measured['dice'])>1e-9:raise AssertionError('Evaluation disagreement')
        config={'schema_version':1,'input':str(data/'source.nii.gz'),'output_root':str(output/'gui-runs'),
                'executable':str(ROOT/'build/desktop/medvessel_cli.exe'),'scales':'0.5,1,1.5,2',
                'seeds':','.join(map(str,seed)),'seeds_in_source':False,'min_intensity':'100','max_intensity':'900',
                'alpha':.5,'beta':.5,'c':100,'low':.08,'high':.2,'connectivity':26,'min_component':20,
                'prune':1,'threads':4,'growth_band_mm':.5,'seed_core_ratio':.35}
        (destination/'gui-config.json').write_text(json.dumps(config,indent=2)+'\n')
        row={'case_id':case,'baseline_dice':original['dice'],'optimized_dice':metric['dice'],
             'dice_change':metric['dice']-original['dice'],'precision':metric['precision'],'recall':metric['recall'],
             'cldice':measured['cldice'],'hd95_mm':measured['hd95_mm'],'python_cpp_voxelwise_match':True,
             'core_threshold':threshold,'core_voxels':int(core.sum()),'mask_voxels':int(actual.sum())}
        rows.append(row)
        new=[r for r in rows if r['case_id']!='1']
        summary={'protocol':protocol,'cases':rows,'additional_mean':{
            key:float(np.mean([r[key] for r in new])) if new else None
            for key in ['baseline_dice','optimized_dice','precision','recall']},
            'all_cases_non_decreasing_dice':all(r['dice_change']>=0 for r in rows)}
        (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(row,flush=True)
    print('C++ AND PYTHON VERIFIED ALL ELEVEN CASES',flush=True)


if __name__=='__main__':main()
