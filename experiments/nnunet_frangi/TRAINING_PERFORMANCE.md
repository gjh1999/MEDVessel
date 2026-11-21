# Training performance experiment — 2026-11-07

Hardware: RTX 5080 16 GB, Windows, 20 logical CPU cores.

The real training augmentation pipeline was benchmarked without loading a model,
using 12 warm-up batches and 100 measured batches per configuration. The main
training continued concurrently, so these are indicative throughput measurements,
not isolated CPU benchmarks. Benchmark outputs and logs are under
`work/full-real-100/loader_benchmark/`.

| Augmentation workers | Seconds / batch | Batches / second |
| --- | ---: | ---: |
| 2 | 0.5042 | 1.98 |
| 4 | 0.2940 | 3.40 |
| 6 | 0.2553 | 3.92 |

Selected 6 training augmentation workers (3 validation workers), configured in
`work/full-real-100/training_performance.json`. Model architecture, batch size 2,
patch size, optimizer, LR schedule, AMP and early stopping are unchanged.

The guardian was stopped deliberately and restarted from the most recently saved
checkpoint. Resume now selects the newer of the periodic latest and best
checkpoints, preserving optimizer state and logged metric history. In this run,
epoch 194 resumed from the best checkpoint rather than the older periodic latest.

Before tuning, epochs took about 113–115 seconds. First tuned epochs: 55.82 and
50.85 seconds (mean 53.34 seconds), approximately 2.1 times the previous speed.
Ten consecutive one-second GPU samples during tuned training were 97–99% busy,
with about 9.5 GB VRAM usage. This supports augmentation throughput as the principal
bottleneck; these short samples are not a full-epoch utilization average.

The regular training guardian and 15-minute local notification reporter remain
enabled. To roll back worker configuration, set `augmentation_workers` to 2 and
restart through the guardian at a saved checkpoint. Changing the JSON does not
alter an already-running training process.
