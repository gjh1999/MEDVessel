import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import contextlib
import io
import nibabel as nib
import numpy as np
import prepare_expanded_ct as prepare


class ExpandedPreparationTest(unittest.TestCase):
    def test_binary_labels_unique_content_and_repeat(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            here = root / 'experiment'
            work = here / 'work/long-run'
            work.mkdir(parents=True)
            train = [f'imagecas_{i}' for i in range(1, 141)]
            val = [f'imagecas_{i}' for i in range(141, 191)]
            (work / 'expanded_cohort.json').write_text(json.dumps(dict(
                train_140=train, validation_50=val, test_sealed=['imagecas_999'], seed=42)))
            original = here / 'work/full-real-100/nnUNet_raw' / prepare.NAME
            for folder in ('imagesTr', 'labelsTr'):
                (original / folder).mkdir(parents=True)
            for i, case in enumerate(train+val):
                for folder, filename, data in (
                    ('imagesTr', case+'_0000.nii.gz', np.full((2, 2, 2), i, dtype=np.float32)),
                    ('labelsTr', case+'.nii.gz', np.array([0, 1, 2, 14, 0, 0, 0, 0], dtype=np.uint8).reshape(2, 2, 2))):
                    image = nib.Nifti1Image(data, np.eye(4))
                    image.header.set_xyzt_units('mm')
                    nib.save(image, original / folder / filename)
            cache = here / 'work/preprocessing-ablation/ct-only/nnUNet_preprocessed' / prepare.NAME
            cache.mkdir(parents=True)
            for name in ('dataset.json', 'nnUNetPlans.json', 'dataset_fingerprint.json'):
                (cache / name).write_text('{}')
            with patch.object(prepare, 'HERE', here), patch.object(prepare, 'ROOT', root), contextlib.redirect_stdout(io.StringIO()):
                prepare.main()
                prepare.main()
            dest = work / 'learning-curve-140/nnUNet_raw' / prepare.NAME
            data = nib.load(dest / 'labelsTr/imagecas_1.nii.gz').get_fdata()
            self.assertEqual(set(np.unique(data)), {0, 1})
            self.assertFalse((dest / 'PREPARATION_INCOMPLETE').exists())
            self.assertFalse((dest / 'imagesTr/imagecas_999_0000.nii.gz').exists())
            self.assertEqual(json.loads((dest / 'dataset.json').read_text())['numTraining'], 190)


if __name__ == '__main__':
    unittest.main()
