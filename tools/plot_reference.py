"""Plot exported volumes with NumPy/Matplotlib; no image processing is applied."""
import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import numpy as np


def read(prefix):
    metadata_path = Path(str(prefix) + ".json")
    metadata = json.loads(metadata_path.read_text())
    if metadata["dtype"] != "float32_le":
        raise ValueError("Unsupported exported scalar type")
    shape = tuple(reversed(metadata["dimensions_xyz"]))
    values = np.fromfile(metadata_path.parent / metadata["raw_file"], dtype="<f4")
    return values.reshape(shape), metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Exported source, vesselness, mask, reference prefixes")
    parser.add_argument("output", type=Path)
    parser.add_argument("--slice", type=int, default=19)
    parser.add_argument("--reference-mask-positive", action="store_true")
    parser.add_argument("--title", default="VMTK aorta sample — qualitative comparison")
    parser.add_argument("--note", default="Reference is an algorithm output, not expert ground truth")
    parser.add_argument("--reference-label", default="VMTK level set")
    args = parser.parse_args()
    source, metadata = read(args.directory / "source")
    response, _ = read(args.directory / "vesselness")
    mask, _ = read(args.directory / "mask")
    reference, _ = read(args.directory / "reference")
    reference = reference > 0 if args.reference_mask_positive else reference < 0
    mask = mask > 0
    if not (source.shape == response.shape == mask.shape == reference.shape):
        raise ValueError("Export grids must match")
    z = min(max(args.slice, 0), source.shape[0] - 1)
    sx, sy, _ = metadata["spacing_mm"]
    extent = (0, source.shape[2] * sx, source.shape[1] * sy, 0)
    vmin, vmax = np.percentile(source, [2, 99])
    fig, axes = plt.subplots(2, 3, figsize=(11, 10), constrained_layout=True)
    for row, selected in enumerate([None, z]):
        image = source.max(axis=0) if selected is None else source[selected]
        vesselness = response.max(axis=0) if selected is None else response[selected]
        prediction = mask.any(axis=0) if selected is None else mask[selected]
        expected = reference.any(axis=0) if selected is None else reference[selected]
        axes[row, 0].imshow(image, cmap="gray", vmin=vmin, vmax=vmax, extent=extent)
        axes[row, 1].imshow(vesselness, cmap="magma", vmin=0, vmax=1, extent=extent)
        axes[row, 2].imshow(image, cmap="gray", vmin=vmin, vmax=vmax, extent=extent)
        axes[row, 2].contour(prediction, levels=[0.5], colors=["#ff8a24"], linewidths=1, extent=extent, origin="upper")
        axes[row, 2].contour(expected, levels=[0.5], colors=["#2cd8ed"], linewidths=1, extent=extent, origin="upper")
        view = "Z maximum projection" if selected is None else f"Z slice {selected}"
        for col, label in enumerate(["Source", "Frangi vesselness", "Prediction / reference"]):
            axes[row, col].set_title(f"{label}\n{view}", fontsize=10)
            axes[row, col].set_xlabel("image x (mm from ROI start)")
            axes[row, col].set_ylabel("image y (mm from ROI start)")
    axes[0, 2].legend(handles=[Patch(color="#ff8a24", label="MEDVessel"), Patch(color="#2cd8ed", label=args.reference_label)], loc="lower right")
    fig.suptitle(args.title + "\n" + args.note, fontsize=12)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)
    plt.close(fig)


if __name__ == "__main__":
    main()
