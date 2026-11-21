"""Pin public benchmark reference files to an actual Git commit."""
import hashlib
import json
from pathlib import Path
from urllib.request import Request, urlopen


def fetch(url):
    with urlopen(Request(url, headers={'User-Agent': 'MEDVessel-research-audit'}), timeout=30) as response:
        return response.read()


def main():
    repo = 'kitbransby/ImageCAS-X'
    commit = json.loads(fetch(f'https://api.github.com/repos/{repo}/commits/main'))
    sha = commit['sha']
    root = Path(__file__).resolve().parents[1] / 'experiments/nnunet_frangi/work/long-run/official-reference' / sha
    root.mkdir(parents=True, exist_ok=True)
    records = []
    for name in ('README.MD', 'configs/nnunet.json', 'configs/pipeline.json', 'train.py'):
        data = fetch(f'https://raw.githubusercontent.com/{repo}/{sha}/{name}')
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        records.append(dict(file=name, sha256=hashlib.sha256(data).hexdigest()))
    (root / 'manifest.json').write_text(json.dumps(dict(commit=sha,
        url=commit['html_url'], files=records,
        scope='Reference only; does not imply exact nnU-Net reproduction'), indent=2))
    print('Pinned official reference:', sha)


if __name__ == '__main__':
    main()
