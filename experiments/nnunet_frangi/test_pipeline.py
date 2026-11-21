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
import prepare_inference
import validate_full_volume
import train_when_ready
from predict import validate_channels
from prepare_dataset import validate_manifest_cases


class PipelineTests(unittest.TestCase):
    def test_training_queue_timeout_never_starts_training(self):
        with tempfile.TemporaryDirectory() as temp, \
             patch.object(sys, 'argv', ['train_when_ready.py', '--work', temp, '--wait-seconds', '0', '--execute-training']), \
             patch.object(train_when_ready.subprocess, 'run') as run:
            with self.assertRaises(TimeoutError):
                train_when_ready.main()
            run.assert_not_called()
            root = Path(temp)
            self.assertFalse((root / 'TRAINING_QUEUE_LOCK').exists())
            self.assertEqual(json.loads((root / 'training_status.json').read_text())['stage'], 'failed')

    def test_full_validation_stops_on_failed_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with patch.object(sys, 'argv', ['validate_full_volume.py', '--work', temp]), \
                 patch.object(validate_full_volume, 'dataset_path', return_value=(root, {
                     'mode': 'full-volume', 'reference_guided_roi': False})), \
                 patch.object(validate_full_volume.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)) as run:
                with self.assertRaisesRegex(RuntimeError, 'check failed'):
                    validate_full_volume.main()
            self.assertEqual(run.call_count, 1)
            self.assertTrue((root / 'FULL_VALIDATION_INCOMPLETE').exists())
            self.assertFalse((root / 'full_validation.json').exists())

    def test_full_volume_preflight_checks_all_cases_before_enhancing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cases = []
            for index in range(3):
                source = root / f'{index}.nii.gz'
                source.write_bytes(b'placeholder')
                cases.append({'case_id': str(index), 'source_file': source.name,
                              'label_file': source.name, 'source_sha256': sha256(source),
                              'label_sha256': sha256(source)})
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'cases': cases, 'complete': True}))
            args = argparse.Namespace(mode='full-volume', data=root, work=root / 'output',
                                      cases=None, enhancer=source, check_only=True)
            with patch('prepare_dataset.enhance') as enhancer:
                prepare(args)
                self.assertFalse(args.work.exists())
                args.check_only = False
                source.write_bytes(b'corrupted final case')
                with self.assertRaisesRegex(ValueError, 'integrity mismatch'):
                    prepare(args)
                enhancer.assert_not_called()
                self.assertFalse(args.work.exists())
            manifest.write_text(json.dumps({'cases': cases, 'complete': False}))
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                prepare(args)

    def test_inference_failure_keeps_marker_and_success_clears_it(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'source.nii.gz'
            image = nib.Nifti1Image(np.ones((4, 4, 4), dtype=np.float32), np.eye(4))
            save_like(np.ones(image.shape, dtype=np.float32), image, source)
            failed = root / 'failed'
            with patch.object(sys, 'argv', ['prepare_inference.py', str(source), str(failed)]), \
                 patch.object(prepare_inference, 'enhance', side_effect=RuntimeError('enhancement failed')):
                with self.assertRaisesRegex(RuntimeError, 'enhancement failed'):
                    prepare_inference.main()
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                validate_channels(failed)
            ready = root / 'ready'
            def fake_enhance(source_path, destination, executable):
                save_like(np.full(image.shape, .5, dtype=np.float32), image, destination)
            with patch.object(sys, 'argv', ['prepare_inference.py', str(source), str(ready)]), \
                 patch.object(prepare_inference, 'enhance', side_effect=fake_enhance):
                prepare_inference.main()
            validate_channels(ready)
            self.assertFalse((ready / 'PREPARATION_INCOMPLETE').exists())
            self.assertFalse(json.loads((ready / 'preparation.json').read_text())['labels_read'])

    def test_manifest_rejects_duplicate_ids_and_escaping_paths(self):
        root = HERE / 'work'
        entry = {'case_id': '1', 'source_file': 'ct.nii.gz', 'label_file': 'label.nii.gz'}
        validate_manifest_cases([entry], root)
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            validate_manifest_cases([entry, dict(entry)], root)
        for case in ('../outside', '', 1):
            with self.assertRaisesRegex(ValueError, 'filename-safe'):
                validate_manifest_cases([{**entry, 'case_id': case}], root)
        with self.assertRaisesRegex(ValueError, 'escapes'):
            validate_manifest_cases([{**entry, 'source_file': '../outside.nii.gz'}], root)

    def test_inference_rejects_incomplete_or_unpaired_channels(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            with self.assertRaises(ValueError):
                validate_channels(directory)
            (directory / 'patient_0000.nii.gz').touch()
            with self.assertRaises(ValueError):
                validate_channels(directory)
            (directory / 'patient_0001.nii.gz').touch()
            validate_channels(directory)
            marker = directory / 'PREPARATION_INCOMPLETE'
            marker.touch()
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                validate_channels(directory)
            marker.unlink()
            (directory / 'unexpected.nii.gz').touch()
            with self.assertRaisesRegex(ValueError, 'filename'):
                validate_channels(directory)

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
        with tempfile.TemporaryDirectory() as temp, \
             patch.object(sys, 'argv', ['workflow.py', 'train', '--dataset', '512', '--work', temp]), \
             patch.object(workflow, 'dataset_path', return_value=(Path(temp), {
                 'mode': 'smoke-roi', 'reference_guided_roi': True})), \
             patch.object(workflow.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'reference-guided'):
                workflow.main()
            run.assert_not_called()

    def test_synthetic_training_guard(self):
        with tempfile.TemporaryDirectory() as temp, \
             patch.object(sys, 'argv', ['workflow.py', 'train', '--work', temp]), \
             patch.object(workflow, 'dataset_path', return_value=(Path(temp), {
                 'data_kind': 'synthetic', 'mode': 'full-volume', 'reference_guided_roi': False})), \
             patch.object(workflow.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'synthetic'):
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
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--report-json', type=Path)
    options, remaining = parser.parse_known_args()
    program = unittest.main(argv=[sys.argv[0], *remaining], exit=False)
    result = program.result
    if options.report_json:
        options.report_json.write_text(json.dumps({
            'tests_run': result.testsRun, 'skipped': len(result.skipped),
            'failures': len(result.failures), 'errors': len(result.errors),
            'passed': result.testsRun - len(result.skipped) - len(result.failures) - len(result.errors),
            'successful': result.wasSuccessful(),
            'skipped_tests': [{'test': str(test), 'reason': reason} for test, reason in result.skipped],
        }, indent=2) + '\n', encoding='utf-8')
    sys.exit(not result.wasSuccessful())
