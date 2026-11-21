"""Offline tests for paired-case selection and archive integrity."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import fetch_imagecas_batch as batch


def archives(corrupt=False):
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for case in range(1, 13):
            z.writestr(f'1-200/{case}.img.nii.gz', bytes([case])*1024)
            z.writestr(f'1-200/{case}.label.nii.gz', b'old label')
    raw = bytearray(inner.getvalue())
    if corrupt:
        raw[14] ^= 1  # First image's expected CRC in local header.
    wrapper = io.BytesIO()
    with zipfile.ZipFile(wrapper, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('1-200.z01', b'PK\x07\x08'+raw)
    labels = io.BytesIO()
    with zipfile.ZipFile(labels, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        # Case 2 has no paired reference and must be skipped.
        for case in [1, *range(3, 13)]:
            z.writestr(f'labels/{case}.coronary.nii.gz', b'new label'+bytes([case]))
    return wrapper.getvalue(), labels.getvalue()


class BatchTests(unittest.TestCase):
    def test_read_crosses_split_volume_boundary(self):
        second = io.BytesIO(b'defgh')
        with patch.object(batch.source, 'OuterZipStream', side_effect=[io.BytesIO(b'abc'), second]), \
             patch.object(batch.source, 'RangeReader', return_value=io.BytesIO()) as reader:
            with batch.SplitVolumeStream(io.BytesIO()) as stream:
                self.assertEqual(stream.read(5), b'abcde')
                self.assertEqual(stream.read(3), b'fgh')
            self.assertIn('1-200.z02', reader.call_args.args[0])

    def run_batch(self, corrupt=False, target_cases=11, target=None, case_ids=None):
        images, labels = archives(corrupt)
        with patch.object(batch.source, 'RangeReader', side_effect=lambda: io.BytesIO(images)), \
             patch.object(batch.labels, 'RemoteZip', side_effect=lambda: io.BytesIO(labels)), \
             contextlib.redirect_stdout(io.StringIO()):
            batch.main(target_cases, target, case_ids=case_ids)

    def test_explicit_selection_resume_and_tamper(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            self.run_batch(target=target, case_ids=['3', '5'])
            manifest = json.loads((target / 'manifest.json').read_text())
            self.assertEqual({r['case_id'] for r in manifest['cases']}, {'3', '5'})
            with patch.object(batch.labels, 'RemoteZip', side_effect=AssertionError('Unexpected network')):
                batch.main(target=target, case_ids=['3', '5'])
            (target / '3.img.nii.gz').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                batch.main(target=target, case_ids=['3', '5'])

    def test_requested_case_count_and_separate_output(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'custom'
            self.run_batch(target_cases=3, target=target)
            manifest = json.loads((target / 'manifest.json').read_text())
            self.assertEqual(manifest['target_cases'], 3)
            self.assertTrue(manifest['complete'])
            self.assertEqual(len(manifest['cases']), 3)
            self.assertEqual(len(list(target.glob('*.img.nii.gz'))), 3)

    def test_eleven_unique_paired_cases_and_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            old = os.getcwd()
            try:
                os.chdir(directory)
                self.run_batch()
                root = Path('datasets/imagecas-multicase')
                manifest = json.loads((root/'manifest.json').read_text())
                self.assertTrue(manifest['complete'])
                self.assertEqual([int(x['case_id']) for x in manifest['cases']], [1, *range(3, 13)])
                self.assertEqual(len(list(root.glob('*.img.nii.gz'))), 11)
                self.assertEqual(len(list(root.glob('*.coronary.nii.gz'))), 11)
                self.assertFalse((root/'2.img.nii.gz').exists())
            finally:
                os.chdir(old)

    def test_corruption_never_commits_case(self):
        with tempfile.TemporaryDirectory() as directory:
            old = os.getcwd()
            try:
                os.chdir(directory)
                with self.assertRaisesRegex(ValueError, 'integrity'):
                    self.run_batch(corrupt=True)
                self.assertFalse(Path('datasets/imagecas-multicase/1.img.nii.gz').exists())
                self.assertFalse(Path('datasets/imagecas-multicase/manifest.json').exists())
            finally:
                os.chdir(old)


if __name__ == '__main__':
    unittest.main()
