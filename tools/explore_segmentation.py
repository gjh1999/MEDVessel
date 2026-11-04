"""Development-only exploration on fixed, saved Frangi volumes. Never edits baseline outputs."""
import json
from pathlib import Path
import time

import numpy as np
from scipy import ndimage
from plot_reference import read

ROOT=Path(__file__).resolve().parents[1]


def score(mask,reference):
    tp=int(np.count_nonzero(mask&reference));p=int(np.count_nonzero(mask));g=int(np.count_nonzero(reference))
    return {'dice':2*tp/(p+g) if p+g else 1.,'precision':tp/p if p else 0.,'recall':tp/g if g else 0.}


def grow(candidate,seed,connectivity):
    components,_=ndimage.label(candidate,structure=ndimage.generate_binary_structure(3,1 if connectivity==6 else 3))
    label=int(components[tuple(reversed(seed))])
    return components==label if label else np.zeros_like(candidate)


def main():
    protocol=json.loads((ROOT/'build/imagecas-multicase/summary.json').read_text())
    configs=[]
    for connectivity in [6,26]:
        for low in [.08,.12,.16,.20,.24,.28,.32,.36,.40]:
            configs.append({'family':'fixed','low':low,'relative':0,'intensity_fraction':0,'connectivity':connectivity})
        for fraction in [.15,.20,.25,.30,.35,.40,.45,.50]:
            configs.append({'family':'seed_response','low':.08,'relative':fraction,'intensity_fraction':0,'connectivity':connectivity})
        for fraction in [.4,.5,.6,.7,.8]:
            for low in [.08,.16,.24]:
                configs.append({'family':'seed_intensity','low':low,'relative':0,'intensity_fraction':fraction,'connectivity':connectivity})
    results=[{'config':c,'cases':[]} for c in configs]
    for case in protocol['evaluated_cases']:
        case_id=case['case_id'];path=ROOT/f'build/imagecas-multicase/case-{case_id}'
        source=read(path/'plots/source')[0];response=read(path/'plots/vesselness')[0]
        reference=read(path/'plots/reference')[0]>0
        seed=json.loads((path/'roi.json').read_text())['seed_voxel'];index=tuple(reversed(seed))
        seed_response=float(response[index]);seed_intensity=float(source[index]);cache={}
        for experiment in results:
            c=experiment['config'];low=max(c['low'],c['relative']*seed_response)
            intensity=max(100,c['intensity_fraction']*seed_intensity)
            key=(low,intensity,c['connectivity'])
            if key not in cache:
                mask=grow((response>=low)&(source>=intensity)&(source<=900),seed,c['connectivity'])
                cache[key]=score(mask,reference)
            experiment['cases'].append({'case_id':case_id,'effective_low':low,'effective_min_intensity':intensity,**cache[key]})
        print('Explored case',case_id,flush=True)
    for experiment in results:
        added=[r for r in experiment['cases'] if r['case_id']!='1']
        experiment['additional_mean']={k:float(np.mean([r[k] for r in added])) for k in ['dice','precision','recall']}
        experiment['additional_min_dice']=min(r['dice'] for r in added)
        experiment['additional_median_dice']=float(np.median([r['dice'] for r in added]))
    results.sort(key=lambda e:e['additional_mean']['dice'],reverse=True)
    output=ROOT/'build/segmentation-exploration';output.mkdir(exist_ok=True)
    (output/'sweep.json').write_text(json.dumps({'scope':'Retrospective development exploration; all labels inspected; not held-out evaluation','experiments':results},indent=2)+'\n')
    for experiment in results[:12]:print(experiment['config'],experiment['additional_mean'],'min',experiment['additional_min_dice'],flush=True)


if __name__=='__main__':main()
