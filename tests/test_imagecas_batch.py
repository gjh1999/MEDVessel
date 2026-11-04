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
    def run_batch(self, corrupt=False):
        images, labels = archives(corrupt)
        with patch.object(batch.source, 'RangeReader', side_effect=lambda: io.BytesIO(images)), \
             patch.object(batch.labels, 'RemoteZip', side_effect=lambda: io.BytesIO(labels)), \
             contextlib.redirect_stdout(io.StringIO()):
            batch.main()

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
