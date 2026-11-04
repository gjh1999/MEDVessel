"""Development sweep: conservative seeded core followed by physically bounded low-response growth."""
import json
from pathlib import Path
import numpy as np
from scipy import ndimage
from plot_reference import read
from explore_segmentation import grow,score

ROOT=Path(__file__).resolve().parents[1]


def main():
    summary=json.loads((ROOT/'build/imagecas-multicase/summary.json').read_text())
    configs=[{'high':high,'relative':relative,'band_mm':band,'connectivity':connectivity}
             for connectivity in [6,26]
             for high,relative in [(.16,0),(.20,0),(.24,0),(.28,0),(.32,0),(.08,.25),(.08,.30),(.08,.35)]
             for band in [.25,.5,.75,1.,1.25,1.5]]
    results=[{'config':c,'cases':[]} for c in configs]
    for entry in summary['evaluated_cases']:
        case=entry['case_id'];directory=ROOT/f'build/imagecas-multicase/case-{case}'
        source,meta=read(directory/'plots/source');v=read(directory/'plots/vesselness')[0]
        ref=read(directory/'plots/reference')[0]>0;seed=json.loads((directory/'roi.json').read_text())['seed_voxel']
        eligible=(source>=100)&(source<=900)&(v>=.08)
        cache={}
        for experiment in results:
            c=experiment['config'];threshold=max(.08,c['high'],c['relative']*float(v[tuple(reversed(seed))]))
            key=(threshold,c['connectivity'])
            if key not in cache:
                core=grow(eligible&(v>=threshold),seed,c['connectivity'])
                distance=ndimage.distance_transform_edt(~core,sampling=meta['spacing_mm'][::-1]) if core.any() else np.full(v.shape,np.inf)
                cache[key]=distance
            mask=grow(eligible&(cache[key]<=c['band_mm']),seed,c['connectivity'])
            experiment['cases'].append({'case_id':case,**score(mask,ref)})
        print('Bounded sweep case',case,flush=True)
    for e in results:
        new=[r for r in e['cases'] if r['case_id']!='1']
        e['additional_mean']={k:float(np.mean([r[k] for r in new])) for k in ['dice','precision','recall']}
        e['additional_min_dice']=min(r['dice'] for r in new)
        e['original_case_dice']=next(r['dice'] for r in e['cases'] if r['case_id']=='1')
    results.sort(key=lambda e:e['additional_mean']['dice'],reverse=True)
    (ROOT/'build/segmentation-exploration/bounded-sweep.json').write_text(json.dumps({'scope':'Development sweep, not independent evaluation','experiments':results},indent=2)+'\n')
    print('TOP ALL',flush=True)
    for e in results[:8]:print(e['config'],e['additional_mean'],'case1',e['original_case_dice'],'min',e['additional_min_dice'],flush=True)
    print('TOP WITH ORIGINAL DICE >= .78',flush=True)
    for e in [e for e in results if e['original_case_dice']>=.78][:8]:print(e['config'],e['additional_mean'],'case1',e['original_case_dice'],'min',e['additional_min_dice'],flush=True)


if __name__=='__main__':main()
