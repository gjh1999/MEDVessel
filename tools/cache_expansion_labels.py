"""Cache only frozen development expansion labels, retaining original ZIP member names."""
import json
from pathlib import Path
import re
import zipfile
import fetch_imagecas_x_label as labels

ROOT = Path(__file__).resolve().parents[1] / 'experiments/nnunet_frangi/work/long-run'


def main():
    selected = set()
    for path in (ROOT / 'source-inventory').glob('extract140_*.json'):
        selected.update(json.loads(path.read_text()))
    target = ROOT / 'source-inventory/expansion_labels.zip'
    partial = target.with_suffix('.partial')
    labels.MAX_TRANSFER = 64 * 1024**2
    found = set()
    with labels.RemoteZip() as remote, zipfile.ZipFile(remote) as source, zipfile.ZipFile(partial, 'w', compression=zipfile.ZIP_STORED) as output:
        for entry in source.infolist():
            match = re.search(r'(?:^|/)(\d+)\.coronary\.nii\.gz$', entry.filename)
            if match and match[1] in selected:
                if entry.file_size > 16*1024**2:
                    raise ValueError('Unexpected label size')
                output.writestr(entry.filename, source.read(entry))
                found.add(match[1])
    if found != selected:
        raise ValueError('Missing requested labels')
    with zipfile.ZipFile(partial) as check:
        if check.testzip() is not None:
            raise ValueError('Cached label integrity failure')
    partial.replace(target)
    print('Cached verified development labels:', len(found), flush=True)


if __name__ == '__main__':
    main()
