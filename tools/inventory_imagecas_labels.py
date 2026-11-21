"""Inspect public label ZIP metadata without downloading test image volumes."""
import json
from pathlib import Path
import re
import zipfile
import fetch_imagecas_x_label as labels


def main():
    out = Path(__file__).resolve().parents[1] / 'experiments/nnunet_frangi/work/long-run/source-inventory'
    out.mkdir(parents=True, exist_ok=True)
    with labels.RemoteZip() as remote, zipfile.ZipFile(remote) as archive:
        rows = [dict(name=p.filename, bytes=p.file_size, crc32=f'{p.CRC:08x}')
                for p in archive.infolist()]
        cases = []
        metadata = []
        for row in rows:
            match = re.search(r'(?:^|/)(\d+)\.coronary\.nii\.gz$', row['name'])
            if match:
                cases.append(match[1])
            name = row['name'].lower()
            if ('filelist/' in name or name.endswith(('descriptors.xlsx', 'train.txt', 'val.txt', 'test.txt', 'exclude.txt'))):
                metadata.append(row)
                if row['bytes'] <= 4 * 1024**2 and not row['name'].endswith('/'):
                    data = archive.read(row['name'])
                    target = out / ('metadata_' + str(len(metadata)) + '_' + Path(row['name']).name)
                    target.write_bytes(data)
                    row['saved_as'] = target.name
        report = dict(source=labels.URL, entries=rows, label_ids=sorted(set(cases), key=int),
                      metadata_candidates=metadata, test_volumes_read=False)
        (out / 'labels_inventory.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(dict(unique_label_ids=len(set(cases)), metadata=metadata)))


if __name__ == '__main__':
    main()
