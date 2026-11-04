"""Explicit paper-style binary 3-D metrics; arrays and spacing use Z,Y,X order."""
import numpy as np
from scipy import ndimage
from skimage.morphology import skeletonize

PROFILE = {
    'name': 'vessel-paper-v1',
    'surface': '6-neighbor foreground boundary voxel centers, exterior is background',
    'hd95': '95th linear percentile of concatenated directed surface distances',
    'assd': 'arithmetic mean of the two directed mean surface distances (MedPy 0.4 convention)',
    'cldice': 'hard clDice with scikit-image Lee 3-D skeletonization',
    'empty': 'both empty: overlap=1, distance=0; one empty: overlap=0, distance=null',
    'distance_unit': 'mm',
}


def evaluate(prediction, reference, spacing_zyx):
    a, b = np.asarray(prediction), np.asarray(reference)
    spacing = np.asarray(spacing_zyx, dtype=float)
    if a.ndim != 3 or a.shape != b.shape:
        raise ValueError('Matching 3-D arrays required')
    if not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError('Non-finite mask values')
    if spacing.shape != (3,) or not np.isfinite(spacing).all() or (spacing <= 0).any():
        raise ValueError('Three positive finite spacings required')
    a, b = a > 0, b > 0
    na, nb = int(a.sum()), int(b.sum())
    tp = int((a & b).sum())
    result = {'dice': 2 * tp / (na + nb) if na + nb else 1.,
              'precision': tp / na if na else (1. if not nb else 0.),
              'recall': tp / nb if nb else (1. if not na else 0.)}
    if not na or not nb:
        result.update(hd95_mm=None if na + nb else 0.,
                      assd_mm=None if na + nb else 0., cldice=0. if na + nb else 1.)
        return result
    structure = ndimage.generate_binary_structure(3, 1)
    sa = a & ~ndimage.binary_erosion(a, structure=structure, border_value=0)
    sb = b & ~ndimage.binary_erosion(b, structure=structure, border_value=0)
    ab = ndimage.distance_transform_edt(~sb, sampling=spacing)[sa]
    ba = ndimage.distance_transform_edt(~sa, sampling=spacing)[sb]
    ska, skb = skeletonize(a, method='lee'), skeletonize(b, method='lee')
    p = np.count_nonzero(ska & b) / np.count_nonzero(ska) if ska.any() else 0.
    r = np.count_nonzero(skb & a) / np.count_nonzero(skb) if skb.any() else 0.
    result.update(hd95_mm=float(np.percentile(np.concatenate((ab, ba)), 95, method='linear')),
                  assd_mm=float((ab.mean() + ba.mean()) / 2),
                  cldice=float(2 * p * r / (p + r)) if p + r else 0.)
    return result
