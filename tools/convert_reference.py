"""Convert a reference image to NIfTI using format I/O only (no filtering)."""
import argparse
import hashlib
import json
from pathlib import Path

import SimpleITK as sitk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--geometry-from", type=Path, help="Explicit mm-declaring NIfTI donor; numeric geometry must already match")
    args = parser.parse_args()
    image = sitk.ReadImage(str(args.source))
    if image.GetDimension() != 3 or image.GetNumberOfComponentsPerPixel() != 1:
        raise ValueError("Only scalar 3D reference images are supported")
    donor = None
    if args.geometry_from:
        donor = sitk.ImageFileReader()
        donor.SetFileName(str(args.geometry_from))
        donor.ReadImageInformation()
        if not donor.HasMetaDataKey("xyzt_units") or int(donor.GetMetaData("xyzt_units")) & 7 != 2:
            raise ValueError("Geometry donor must explicitly declare millimetre units")
        if image.GetSize() != donor.GetSize():
            raise ValueError("Reference and donor dimensions differ")
        for actual, expected in [(image.GetSpacing(), donor.GetSpacing()), (image.GetOrigin(), donor.GetOrigin()), (image.GetDirection(), donor.GetDirection())]:
            if any(abs(a - b) > 1e-6 for a, b in zip(actual, expected)):
                raise ValueError("Reference and donor geometry differ; refusing to infer alignment")
        image.SetSpacing(donor.GetSpacing())
        image.SetOrigin(donor.GetOrigin())
        image.SetDirection(donor.GetDirection())
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    sitk.WriteImage(image, str(args.destination), True)
    metadata = {
        "source": str(args.source.resolve()),
        "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        "destination_sha256": hashlib.sha256(args.destination.read_bytes()).hexdigest(),
        "converter": "SimpleITK format I/O only",
        "converter_version": sitk.Version_VersionString(),
        "dimensions": image.GetSize(),
        "spacing": image.GetSpacing(),
        "origin_lps": image.GetOrigin(),
        "direction_lps": image.GetDirection(),
    }
    if donor is not None:
        metadata["geometry_donor"] = str(args.geometry_from.resolve())
        metadata["geometry_donor_sha256"] = hashlib.sha256(args.geometry_from.read_bytes()).hexdigest()
        metadata["geometry_policy"] = "Numeric geometry verified against an explicitly mm-declaring paired image; original label preserved"
    args.destination.with_suffix(args.destination.suffix + ".conversion.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
