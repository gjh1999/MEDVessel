"""Download eleven archive-order CTA cases (ten new) paired with ImageCAS-X v2 labels."""
import binascii
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import zipfile
from contextlib import nullcontext


class LocalSplitStream:
    """Read the original split archive directly from a downloaded outer ZIP."""
    def __init__(self, path, group='1-200'):
        self.archive = zipfile.ZipFile(path)
        if group not in ('1-200', '201-400', '401-600', '601-800', '801-1000'):
            raise ValueError('Unknown ImageCAS archive group')
        self.members = iter([f'{group}.z{i:02d}' for i in range(1, 5)] + [f'{group}.change2zip'])
        self.stream = self.archive.open(next(self.members))

    def read(self, count):
        result = bytearray()
        while len(result) < count:
            block = self.stream.read(count - len(result))
            if block:
                result.extend(block)
            else:
                self.stream.close()
                member = next(self.members, None)
                if member is None:
                    break
                self.stream = self.archive.open(member)
        return bytes(result)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.stream.close()
        self.archive.close()

import fetch_imagecas_sample as source
import fetch_imagecas_x_label as labels


def save_json(path, value):
    partial = path.with_suffix('.partial')
    partial.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(partial, path)


class SplitVolumeStream:
    """Join decompressed ZIP split volumes, including headers crossing a volume edge."""
    def __init__(self, first_response):
        self.stream = source.OuterZipStream(first_response)
        self.volume = 1
        self.extra_response = None

    def read(self, count):
        data = bytearray()
        while len(data) < count:
            block = self.stream.read(count - len(data))
            if block:
                data.extend(block)
                continue
            if self.extra_response is not None:
                self.extra_response.close()
            self.volume += 1
            if self.volume > 8:
                raise ValueError('Exceeded bounded split-volume count')
            member = f'1-200.z{self.volume:02d}'
            print(f'Continuing archive volume: {member}', flush=True)
            self.extra_response = source.RangeReader(source.URL.replace('1-200.z01', member))
            self.stream = source.OuterZipStream(self.extra_response, expected_member=member)
        return bytes(data)

    def close(self):
        if self.extra_response is not None:
            self.extra_response.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def main(target_cases=11, target=None, local_archive=None, case_ids=None, group='1-200', local_labels=None):
    selected = set(map(str, case_ids)) if case_ids is not None else None
    if selected is not None:
        if not selected or any(not case.isdigit() for case in selected):
            raise ValueError('Expected nonempty numeric case IDs')
        low, high = map(int, group.split('-'))
        if any(not low <= int(case) <= high for case in selected):
            raise ValueError('Selected case outside archive group')
        target_cases = len(selected)
    if group != '1-200' and not local_archive:
        raise ValueError('Additional groups currently require a local archive')
    if not 1 <= target_cases <= 200:
        raise ValueError('Select between 1 and 200 paired cases from the first ImageCAS split')
    target = Path(target) if target is not None else Path('datasets/imagecas-multicase')
    target.mkdir(parents=True, exist_ok=True)
    source.LIMIT = max(2, (target_cases + 3) // 4) * 1024**3
    labels.MAX_TRANSFER = max(128, target_cases * 4) * 1024**2
    if shutil.disk_usage(target).free < max(8, target_cases // 3) * 1024**3:
        raise RuntimeError('Insufficient free space for bounded download and cache')
    manifest_path = target / 'manifest.json'
    manifest = {'source': 'ImageCAS v3', 'reference': 'ImageCAS-X v2, Zenodo 23562647',
                'target_cases': target_cases, 'selection': 'first requested number of archive-order images having an ImageCAS-X label; no metric-based selection',
                'existing_development_case': '1', 'cases': [], 'complete': False}
    if selected is not None:
        manifest.update(selection='Explicit case IDs; no metric-based selection',
                        selected_case_ids=sorted(selected, key=int), archive_group=group)
    verified = set()
    if manifest_path.exists():
        old = json.loads(manifest_path.read_text())
        for key in ('target_cases', 'selection', 'selected_case_ids', 'archive_group'):
            if old.get(key) != manifest.get(key):
                raise ValueError('Existing manifest selection mismatch')
        for row in old['cases']:
            for file_key, hash_key in (('source_file', 'source_sha256'), ('label_file', 'label_sha256')):
                digest = hashlib.sha256()
                with (target / row[file_key]).open('rb') as existing:
                    for block in iter(lambda: existing.read(1024**2), b''):
                        digest.update(block)
                if digest.hexdigest() != row[hash_key]:
                    raise ValueError('Existing case failed checksum verification')
            verified.add(row['case_id'])
        manifest = old
        if len(verified) == target_cases:
            print('All requested cases already verified', flush=True)
            return
    with (nullcontext(Path(local_labels)) if local_labels else labels.RemoteZip()) as remote, zipfile.ZipFile(remote) as archive:
        entries = {}
        for entry in archive.infolist():
            match = re.search(r'(?:^|/)(\d+)\.coronary\.nii\.gz$', entry.filename)
            if match:
                entries[match[1]] = entry
        if selected is not None and not selected.issubset(entries):
            raise ValueError('Requested IDs missing from ImageCAS-X labels')
        with (nullcontext() if local_archive else source.RangeReader()) as response, (LocalSplitStream(local_archive, group) if local_archive else SplitVolumeStream(response)) as stream:
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
                if not case or case not in entries or case in verified or (selected is not None and case not in selected):
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
                verified.add(case)
                manifest['complete'] = len(manifest['cases']) == target_cases
                save_json(manifest_path, manifest)
                print(f'Verified paired cases: {len(manifest["cases"])}/{target_cases}; case={case}', flush=True)
                if manifest['complete']:
                    break
                signature = source.exact(stream, 4)
    if not manifest['complete']:
        raise RuntimeError('Bounded archive prefix did not contain the requested paired cases')
    print(f'DOWNLOAD COMPLETE: {target_cases} paired cases.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target-cases', type=int, default=11)
    parser.add_argument('--output', type=Path, default=Path('datasets/imagecas-multicase'))
    parser.add_argument('--local-archive', type=Path)
    parser.add_argument('--case-list', type=Path, help='JSON list of numeric IDs, frozen before extraction')
    parser.add_argument('--local-labels', type=Path, help='Validated local ZIP of selected original labels')
    parser.add_argument('--group', default='1-200', choices=['1-200', '201-400', '401-600', '601-800', '801-1000'])
    args = parser.parse_args()
    main(args.target_cases, args.output, args.local_archive,
         json.loads(args.case_list.read_text()) if args.case_list else None, args.group, args.local_labels)
