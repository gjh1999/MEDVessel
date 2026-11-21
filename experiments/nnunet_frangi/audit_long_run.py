"""Capture reproducible stage-zero evidence without reading held-out volumes."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out = HERE / 'work/long-run/audits' / stamp
    out.mkdir(parents=True, exist_ok=False)
    def capture(name, command):
        result = subprocess.run(command, cwd=ROOT, capture_output=True)
        (out / name).write_bytes(result.stdout)
        (out / (name + '.stderr')).write_bytes(result.stderr)
        return result.returncode
    codes = {}
    for name, command in [
        ('changes.patch', ['git', 'diff', '--binary', 'HEAD']),
        ('git-status.txt', ['git', 'status', '--porcelain']),
        ('dependencies.txt', [sys.executable, '-m', 'pip', 'freeze']),
        ('gpu.txt', ['nvidia-smi']),
    ]:
        codes[name] = capture(name, command)
    inventory = []
    patterns = ('*plans*.json', '*split*.json', '*manifest*.json', '*status*.json',
                '*results*.json', 'validation_cases.json', '*holdout*.json')
    files = set()
    for pattern in patterns:
        files.update((HERE / 'work').rglob(pattern))
    for source in sorted(files):
        if out.parent.parent in source.parents:
            continue
        relative = source.relative_to(ROOT)
        data = source.read_bytes()
        snapshot = hashlib.sha256(str(relative).encode()).hexdigest() + '.json'
        target = out / 'metadata' / snapshot
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        inventory.append(dict(path=str(relative), snapshot=snapshot, bytes=len(data),
                              sha256=hashlib.sha256(data).hexdigest()))
    sources = list(HERE.glob('*.py')) + list((ROOT / 'tools').glob('*.py'))
    for source in sources:
        target = out / 'code' / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    disk = shutil.disk_usage(ROOT)
    report = dict(created_utc=stamp, interpreter=sys.executable,
                  disk=dict(total=disk.total, used=disk.used, free=disk.free),
                  command_return_codes=codes, metadata=inventory,
                  heldout_volumes_read=False, training_started=False,
                  next_step='Implement and simulate persistent queue before real training')
    (out / 'audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(output=str(out), free_gib=round(disk.free / 2**30, 2),
                          metadata_files=len(inventory), command_return_codes=codes)))


if __name__ == '__main__':
    main()
