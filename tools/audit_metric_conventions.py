"""Read-only comparison of documented metric conventions on the same saved masks."""
import json
from pathlib import Path
import numpy as np
from scipy import ndimage
from skimage.morphology import skeletonize
import skimage
from plot_reference import read

ROOT=Path(__file__).resolve().parents[1]


def surface(mask):
    return mask & ~ndimage.binary_erosion(mask,structure=ndimage.generate_binary_structure(3,1),border_value=0)


def distances(a,b,spacing):
    return ndimage.distance_transform_edt(~b,sampling=spacing)[a]


def cldice_lee(mask,reference,reference_skeleton):
    skel=skeletonize(mask,method='lee').astype(bool)
    if not skel.any() or not reference_skeleton.any():return 0.
    precision=np.count_nonzero(skel&reference)/np.count_nonzero(skel)
    recall=np.count_nonzero(reference_skeleton&mask)/np.count_nonzero(reference_skeleton)
    return 2*precision*recall/(precision+recall) if precision+recall else 0.


def main():
    summary=json.loads((ROOT/'build/segmentation-optimized/summary.json').read_text())
    rows=[]
    for row in summary['cases']:
        case=row['case_id'];old=ROOT/f'build/imagecas-multicase/case-{case}'
        reference,meta=read(old/'plots/reference');reference=reference>0
        reference_skeleton=skeletonize(reference,method='lee').astype(bool)
        reference_surface=surface(reference)
        spacing=meta['spacing_mm'][::-1]
        for variant,path in [('baseline',old),('core_growth',ROOT/f'build/segmentation-optimized/case-{case}')]:
            prediction=read(path/'plots/mask')[0]>0
            original=json.loads((path/'result/metrics.json').read_text())
            a=surface(prediction)
            ab=distances(a,reference_surface,spacing);ba=distances(reference_surface,a,spacing)
            hd95_directed=max(float(np.quantile(ab,.95,method='inverted_cdf')),float(np.quantile(ba,.95,method='inverted_cdf')))
            if abs(hd95_directed-original['hd95_mm'])>1e-4:raise AssertionError('C++ HD95 differs from its documented convention')
            pooled=float(np.percentile(np.concatenate([ab,ba]),95))
            unweighted_surface_assd=float((ab.sum()+ba.sum())/(len(ab)+len(ba)))
            value={'case_id':case,'variant':variant,'dice':original['dice'],
                'hd95_cpp_directed_max_nearest_rank_mm':original['hd95_mm'],
                'hd95_pooled_linear_percentile_mm':pooled,
                'surface_assd_point_weighted_mm':unweighted_surface_assd,
                'cldice_cpp_custom_thinning':original['cldice'],
                'cldice_lee_skimage':float(cldice_lee(prediction,reference,reference_skeleton))}
            rows.append(value)
            print(value,flush=True)
    means={}
    for variant in ['baseline','core_growth']:
        group=[r for r in rows if r['variant']==variant and r['case_id']!='1']
        means[variant]={key:float(np.mean([r[key] for r in group])) for key in rows[0] if key not in ['case_id','variant']}
    output={'scope':'Convention audit of fixed development masks; no changes to historical C++ metrics',
            'skimage_version':skimage.__version__,'skeleton_method':'lee',
            'surface_definition':'6-neighbor foreground boundary voxel centers; point-weighted, not surfel-area-weighted',
            'pooled_percentile':'NumPy linear percentile of concatenated directed boundary-distance arrays; MedPy-style, not calling MedPy',
            'cases':rows,'additional_means':means}
    (ROOT/'build/segmentation-optimized/metric-conventions.json').write_text(json.dumps(output,indent=2)+'\n')
    print('ADDITIONAL MEANS',json.dumps(means,indent=2),flush=True)


if __name__=='__main__':main()
