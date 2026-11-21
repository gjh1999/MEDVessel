# Accuracy-first experiment plan

User priority: accuracy over inference speed (2026-11-07).
Working research target, pending user confirmation: mean full-volume Dice >= 0.90.
This is a research target, not an achieved or guaranteed result.

Baseline: 70 training / 15 validation / 15 internal test cases, ImageCAS CT and
ImageCAS-X v2 labels. Final checkpoint plain-inference validation Dice 0.75934,
precision 0.66778, recall 0.90416. The default nnU-Net validation Dice was 0.77066;
these are different inference configurations and must be reported separately.

1. Complete current validation comparisons: best/final weights and mirroring TTA.
2. Study false-positive components using validation data only. Run
   `tune_components.py`; inspect Dice, precision, recall and distal-branch retention.
   Thresholds are physical volumes (mm^3), not voxel counts. Preserve raw masks.
3. Visually inspect worst cases before adopting filtering. Do not assume that
   small disconnected vessels are false positives or keep only the largest vessel.
4. For further training, compare a lower-learning-rate continuation in an isolated
   result directory against the frozen baseline. Retain optimizer/history and
   explicitly record any revised LR schedule and early-stopping policy. The
   present run stopped while LR remained relatively high; more epochs alone are
   not evidence of improvement.
5. Add a CT-only ablation with the same split/budget before attributing benefit to
   Frangi. Expand training data without including existing validation/test cases.
6. Select models and postprocessing on validation only. Do not use internal test
   results as a tuning signal; if the current baseline pipeline evaluates that
   split, retain it as a reported baseline and do not optimize against its scores.

Also report clDice, precision/recall, surface distances and per-case spread. A
mean Dice target alone does not establish clinical suitability or connectivity.
