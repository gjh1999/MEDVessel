"""Validation-only component audit and representative error slices."""
import csv
import json
from pathlib import Path
import numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw
from prepare_dataset import load_volume, check_grid

HERE = Path(__file__).resolve().parent
BASE = HERE / 'work/full-real-100'
PAIR = HERE / 'work/preprocessing-ablation'
OUT = HERE / 'work/long-run/diagnostics'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cases = json.loads((BASE / 'validation_cases.json').read_text())
    holdout = json.loads((BASE / 'nnUNet_raw/Dataset511_CoronaryFrangi/holdout.json').read_text())
    assert set(cases) == set(holdout['val']) and not set(cases) & set(holdout['test'])
    rows = []
    for case in cases:
        ct_image, ct = load_volume(BASE / 'validation_images' / (case + '_0000.nii.gz'))
        ref_image, truth = load_volume(BASE / 'nnUNet_raw/Dataset511_CoronaryFrangi/labelsTr' / (case + '.nii.gz'))
        check_grid(ct_image, ref_image)
        truth = truth > 0
        for arm in ('ct-only', 'dual'):
            image, pred = load_volume(PAIR / arm / 'unified_validation_predictions' / (case + '.nii.gz'))
            check_grid(image, ref_image)
            pred = pred > 0
            fp, fn = pred & ~truth, truth & ~pred
            components, count = ndimage.label(fp)
            sizes = np.bincount(components.ravel())[1:]
            voxel = abs(np.linalg.det(image.affine[:3, :3]))
            row = dict(case_id=case, arm=arm, false_positive_mm3=float(fp.sum()*voxel),
                       false_negative_mm3=float(fn.sum()*voxel), fp_components=count,
                       largest_fp_mm3=float(sizes.max()*voxel) if sizes.size else 0,
                       dice=float(2*(pred & truth).sum()/max(1, pred.sum()+truth.sum())))
            rows.append(row)
            # Select errors on all three axes; these slices are review aids, not a diagnosis.
            panels = []
            for axis in range(3):
                sums = (fp | fn).sum(axis=tuple(i for i in range(3) if i != axis))
                index = int(sums.argmax())
                gray = (np.clip((np.take(ct, index, axis)+200)/1200, 0, 1)*255).astype('uint8')
                rgb = np.repeat(gray[..., None], 3, axis=2)
                rgb[np.take(fp, index, axis)] = [255, 70, 70]
                rgb[np.take(fn, index, axis)] = [60, 130, 255]
                rgb[np.take(pred & truth, index, axis)] = [60, 220, 100]
                panel = Image.fromarray(np.rot90(rgb)).resize((512, 512))
                ImageDraw.Draw(panel).text((8, 8), f'axis {axis}, slice {index}', fill='white')
                panels.append(panel)
            canvas = Image.new('RGB', (1536, 548))
            for i, panel in enumerate(panels):
                canvas.paste(panel, (512*i, 36))
            ImageDraw.Draw(canvas).text((8, 8), f'{case} {arm}: red=FP blue=FN green=TP; voxel-plane view', fill='white')
            canvas.save(OUT / f'{case}_{arm}.png')
            print(case, arm, row['dice'], flush=True)
    with (OUT / 'cases.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (OUT / 'summary.json').write_text(json.dumps(dict(validation_only=True,
        holdout_evaluated=False, cases=rows, note='Manual anatomical review required; FP components use 6-connectivity.'), indent=2))


if __name__ == '__main__':
    main()
