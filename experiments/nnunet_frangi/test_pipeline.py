"""Meaningful data/geometry/leakage checks; no training or weight updates."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import nibabel as nib
import numpy as np
from prepare_dataset import ROOT, HERE, check_grid, load_volume, save_like, fixed_split, prepare, sha256
import workflow
import evaluate_predictions


class PipelineTests(unittest.TestCase):
    def test_split_is_fixed_disjoint_and_complete(self):
        ids = [f'case_{n}' for n in range(11)]
        a, b = fixed_split(ids)[0], fixed_split(list(reversed(ids)))[0]
        self.assertEqual(a, b)
        self.assertEqual((len(a['train']), len(a['val'])), (8, 3))
        self.assertFalse(set(a['train']) & set(a['val']))
        self.assertEqual(set(a['train'] + a['val']), set(ids))

    def test_grid_mismatch_rejected(self):
        a = nib.Nifti1Image(np.zeros((3, 3, 3)), np.eye(4))
        affine = np.eye(4); affine[0, 3] = 1
        b = nib.Nifti1Image(np.zeros((3, 3, 3)), affine)
        with self.assertRaises(ValueError):
            check_grid(a, b)

    def test_geometry_roundtrip_and_nonfinite_rejected(self):
        with tempfile.TemporaryDirectory(dir=HERE / 'work') as temp:
            path = Path(temp) / 'image.nii.gz'
            affine = np.diag([-.4, .5, .8, 1.]); affine[:3, 3] = [10, 20, 30]
            image = nib.Nifti1Image(np.ones((4, 5, 6), dtype=np.float32), affine)
            save_like(np.ones((4, 5, 6), dtype=np.float32), image, path)
            loaded, data = load_volume(path)
            self.assertTrue(np.allclose(loaded.affine, affine))
            self.assertTrue(np.array_equal(data, np.ones((4, 5, 6))))
            data[0, 0, 0] = np.nan
            save_like(data, image, path)
            with self.assertRaises(ValueError):
                load_volume(path)

    def test_unknown_units_rejected(self):
        with tempfile.TemporaryDirectory(dir=HERE / 'work') as temp:
            path = Path(temp) / 'image.nii.gz'
            nib.save(nib.Nifti1Image(np.ones((3, 3, 3)), np.eye(4)), path)
            with self.assertRaises(ValueError):
                load_volume(path)

    def test_smoke_training_guard(self):
        with patch.object(sys, 'argv', ['workflow.py', 'train', '--dataset', '512']), \
             patch.object(workflow.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'reference-guided'):
                workflow.main()
            run.assert_not_called()

    def test_evaluation_identity_and_missing_case(self):
        with tempfile.TemporaryDirectory(dir=HERE / 'work') as temp:
            directory = Path(temp)
            mask = np.zeros((8, 8, 8), dtype=np.uint8); mask[2:6, 4, 4] = 1
            image = nib.Nifti1Image(mask, np.diag([.5, .5, 1., 1.]))
            save_like(mask, image, directory / 'patient.nii.gz')
            case_list = directory / 'cases.json'; case_list.write_text('["patient"]')
            output = directory / 'metrics.json'
            args = ['evaluate_predictions.py', str(directory), str(directory), str(output), '--case-list', str(case_list)]
            with patch.object(sys, 'argv', args):
                evaluate_predictions.main()
            scores = json.loads(output.read_text())['cases'][0]
            self.assertEqual((scores['dice'], scores['cldice'], scores['hd95_mm'], scores['assd_mm']), (1, 1, 0, 0))
            case_list.write_text('["patient", "missing_patient"]')
            with patch.object(sys, 'argv', args), self.assertRaises(FileNotFoundError):
                evaluate_predictions.main()

    def test_full_volume_preparation_and_image_only_enhancement(self):
        executable = ROOT / 'build/cmake/experiments/nnunet_frangi/medvessel_enhance.exe'
        if not executable.exists():
            self.skipTest('Build medvessel_enhance for the real enhancement integration test')
        with tempfile.TemporaryDirectory(dir=HERE / 'work') as temp:
            data = Path(temp) / 'sources'; data.mkdir()
            xyz = np.indices((24, 24, 24), dtype=float)
            ct = (400 * np.exp(-((xyz[0]-12)**2 + (xyz[1]-12)**2) / 8)).astype(np.float32)
            affine = np.diag([-.5, .5, 1., 1.]); affine[0, 3] = 50
            image = nib.Nifti1Image(ct, affine)
            cases = []
            for index in range(3):
                source = data / f'{index}.nii.gz'; label = data / f'{index}.label.nii.gz'
                save_like(ct, image, source)
                labels = np.zeros(ct.shape, dtype=np.uint8)
                labels[10+index:13+index, 10:13, :] = 1  # Different GT, identical CT.
                save_like(labels, image, label)
                cases.append({'case_id': str(index), 'source_file': source.name, 'label_file': label.name,
                              'source_sha256': sha256(source), 'label_sha256': sha256(label)})
            (data / 'manifest.json').write_text(json.dumps({'cases': cases}))
            work = Path(temp) / 'output'
            prepare(argparse.Namespace(mode='full-volume', data=data, work=work, cases=None, enhancer=executable))
            dataset = next((work / 'nnUNet_raw').iterdir())
            self.assertFalse((dataset / 'PREPARATION_INCOMPLETE').exists())
            channels = [load_volume(dataset / 'imagesTr' / f'imagecas_{i}_0001.nii.gz')[1] for i in range(3)]
            self.assertTrue(np.array_equal(channels[0], channels[1]))
            self.assertTrue(np.array_equal(channels[1], channels[2]))
            self.assertGreater(channels[0].max(), 0)
            self.assertTrue(np.array_equal(load_volume(dataset / 'imagesTr/imagecas_0_0000.nii.gz')[1], ct))
            self.assertEqual(len(list((dataset / 'labelsTr').glob('*.nii.gz'))), 3)


if __name__ == '__main__':
    (HERE / 'work').mkdir(exist_ok=True)
    unittest.main()
