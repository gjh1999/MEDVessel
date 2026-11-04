"""Download eleven archive-order CTA cases (ten new) paired with ImageCAS-X v2 labels."""
import binascii
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import zipfile

import fetch_imagecas_sample as source
import fetch_imagecas_x_label as labels


def save_json(path, value):
    partial = path.with_suffix('.partial')
    partial.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(partial, path)


def main():
    target = Path('datasets/imagecas-multicase')
    target.mkdir(parents=True, exist_ok=True)
    source.LIMIT = 2 * 1024**3
    labels.MAX_TRANSFER = 128 * 1024**2
    if shutil.disk_usage(target).free < 8 * 1024**3:
        raise RuntimeError('At least 8 GiB free space is required')
    manifest_path = target / 'manifest.json'
    manifest = {'source': 'ImageCAS v3', 'reference': 'ImageCAS-X v2, Zenodo 23562647',
                'target_cases': 11, 'selection': 'first eleven archive-order images having an ImageCAS-X label; no metric-based selection',
                'existing_development_case': '1', 'cases': [], 'complete': False}
    with labels.RemoteZip() as remote, zipfile.ZipFile(remote) as archive:
        entries = {}
        for entry in archive.infolist():
            match = re.search(r'(?:^|/)(\d+)\.coronary\.nii\.gz$', entry.filename)
            if match:
                entries[match[1]] = entry
        with source.RangeReader() as response:
            stream = source.OuterZipStream(response)
            signature = source.exact(stream, 4)
            if signature == b'PK\x07\x08':
                signature = source.exact(stream, 4)
            for _ in range(500):
                if signature != b'PK\x03\x04':
                    raise ValueError('Unexpected inner ZIP signature before batch completed')
                header = source.HEADER.unpack(signature + source.exact(stream, source.HEADER.size - 4))
                flags, method, crc_expected, compressed, size_expected = header[2], header[3], header[6], header[7], header[8]
                name = source.exact(stream, header[-2]).decode('utf-8')
                source.exact(stream, header[-1])
                if flags & 9 or method not in (0, 8) or max(compressed, size_expected) > 512 * 1024**2:
                    raise ValueError('Unsupported archive member encoding or size')
                match = re.search(r'(?:^|/)(\d+)\.img\.nii\.gz$', name)
                case = match[1] if match else None
                if not case or case not in entries:
                    remaining = compressed
                    while remaining:
                        block = source.exact(stream, min(source.BLOCK, remaining))
                        remaining -= len(block)
                    signature = source.exact(stream, 4)
                    continue
                path = target / f'{case}.img.nii.gz'
                print(f'Case {case}: CTA {size_expected / 1024**2:.1f} MiB', flush=True)
                partial = path.with_suffix('.partial')
                decoder = source.zlib.decompressobj(-15) if method == 8 else None
                remaining = compressed
                crc = written = 0
                digest = hashlib.sha256()
                with partial.open('wb') as out:
                    while remaining:
                        block = source.exact(stream, min(source.BLOCK, remaining))
                        remaining -= len(block)
                        data = decoder.decompress(block, size_expected - written + 1) if decoder else block
                        written += len(data)
                        if written > size_expected or (decoder and decoder.unconsumed_tail):
                            raise ValueError('Archive member exceeds declared size')
                        crc = binascii.crc32(data, crc)
                        digest.update(data)
                        out.write(data)
                if written != size_expected or crc != crc_expected or (decoder and not decoder.eof):
                    raise ValueError('CTA integrity verification failed')
                os.replace(partial, path)
                entry = entries[case]
                if entry.file_size > 16 * 1024**2:
                    raise ValueError('Unexpected label size')
                label = archive.read(entry)
                label_path = target / f'{case}.coronary.nii.gz'
                partial = label_path.with_suffix('.partial')
                partial.write_bytes(label)
                os.replace(partial, label_path)
                manifest['cases'].append({'case_id': case, 'source_file': path.name, 'source_bytes': written,
                    'source_crc32': f'{crc:08x}', 'source_sha256': digest.hexdigest(),
                    'label_file': label_path.name, 'label_member': entry.filename,
                    'label_crc32': f'{entry.CRC:08x}', 'label_sha256': hashlib.sha256(label).hexdigest()})
                manifest['complete'] = len(manifest['cases']) == 11
                save_json(manifest_path, manifest)
                print(f'Verified paired cases: {len(manifest["cases"])}/11; case={case}', flush=True)
                if manifest['complete']:
                    break
                signature = source.exact(stream, 4)
    if not manifest['complete']:
        raise RuntimeError('Bounded archive prefix did not contain eleven paired cases')
    print('DOWNLOAD COMPLETE: eleven paired cases, ten additional to case 1.', flush=True)


if __name__ == '__main__':
    main()
