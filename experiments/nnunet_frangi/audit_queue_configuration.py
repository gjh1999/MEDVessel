"""Verify batch dependencies and frozen scan isolation without reading volumes."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent / 'work/long-run'


def main():
    cohort = json.loads((ROOT / 'expanded_cohort.json').read_text())
    assert set(cohort['train_35']) <= set(cohort['train_70']) <= set(cohort['train_140']) <= set(cohort['train_280'])
    assert not set(cohort['train_280']) & set(cohort['validation_50'])
    assert not set(cohort['train_280'] + cohort['validation_50']) & set(cohort['test_sealed'])
    assert len(set(cohort['train_280'] + cohort['validation_50'] + cohort['test_sealed'])) == 345
    jobs = json.loads((ROOT / 'batch01_with_evaluation.json').read_text())['jobs']
    seen = set()
    hours = 0
    for job in jobs:
        assert set(job.get('depends', [])) <= seen
        assert job['id'] not in seen
        seen.add(job['id'])
        hours += job.get('estimated_gpu_hours', 0)
    assert hours <= 60
    report = dict(job_ids=sorted(seen), estimated_gpu_hours=hours,
        unique_scan_ids=345, test_overlap=0, patient_independence_verified=False)
    (ROOT / 'queue_configuration_audit.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
