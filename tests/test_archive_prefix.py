"""Offline integrity checks for the bounded public sample downloader."""
import importlib.util
import io
import json
import os
from pathlib import Path
import random
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

module_path = Path(__file__).resolve().parents[1] / "tools/fetch_imagecas_sample.py"
spec = importlib.util.spec_from_file_location("sample_download", module_path)
sample = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sample)


def wrapped_archive(corrupt_crc=False):
    data = random.Random(42).randbytes(8192)
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("1-200/", b"")
        archive.writestr("1-200/1.img.nii.gz", data)
        archive.writestr("1-200/1.label.nii.gz", data[:256])
    raw = bytearray(inner.getvalue())
    if corrupt_crc:
        # Skip the directory entry and damage the image's advertised CRC.
        header = sample.HEADER.unpack(raw[:30])
        image_offset = 30 + header[-2] + header[-1] + header[7]
        crc = struct.unpack_from("<I", raw, image_offset + 14)[0]
        struct.pack_into("<I", raw, image_offset + 14, crc ^ 1)
    outer = io.BytesIO()
    with zipfile.ZipFile(outer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("1-200.z01", b"PK\x07\x08" + raw)
    return outer.getvalue(), data


class ArchivePrefixTests(unittest.TestCase):
    def run_downloader(self, body):
        with patch.object(sample, "RangeReader", lambda: io.BytesIO(body)):
            sample.main()

    def test_complete_members_and_cached_integrity(self):
        body, expected = wrapped_archive()
        with tempfile.TemporaryDirectory(prefix="medvessel-archive-") as directory:
            previous = Path.cwd()
            try:
                os.chdir(directory)
                self.run_downloader(body)
                root = Path("datasets/imagecas-sample")
                self.assertEqual((root / "1.img.nii.gz").read_bytes(), expected)
                self.assertEqual((root / "1.label.nii.gz").read_bytes(), expected[:256])
                self.assertEqual(len(json.loads((root / "manifest.json").read_text())["files"]), 2)
                self.run_downloader(body)  # Validate cache without making a network call.
                (root / "1.img.nii.gz").write_bytes(b"damaged")
                with self.assertRaises(ValueError):
                    self.run_downloader(body)
            finally:
                os.chdir(previous)

    def test_corrupt_member_never_committed(self):
        body, _ = wrapped_archive(corrupt_crc=True)
        with tempfile.TemporaryDirectory(prefix="medvessel-archive-") as directory:
            previous = Path.cwd()
            try:
                os.chdir(directory)
                with self.assertRaises(ValueError):
                    self.run_downloader(body)
                self.assertFalse(Path("datasets/imagecas-sample/1.img.nii.gz").exists())
                self.assertFalse(Path("datasets/imagecas-sample/manifest.json").exists())
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    unittest.main()
