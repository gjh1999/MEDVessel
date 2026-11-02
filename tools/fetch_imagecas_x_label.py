"""Fetch one ImageCAS-X v2 label using a bounded remote ZIP reader."""
import hashlib
import io
import json
from pathlib import Path
from urllib.request import Request, urlopen
import zipfile

URL = "https://zenodo.org/records/23562647/files/ImageCAS-X_dataset_v2.zip?download=1"
MAX_TRANSFER = 16 * 1024 * 1024
BLOCK = 256 * 1024
EXPECTED_SHA256 = "48fc4523a9e5cc704b2a453ab7771023d7ba2d9c3ad8096f4259bbec0f1b73b8"


class RemoteZip(io.RawIOBase):
    def __init__(self):
        super().__init__()
        with urlopen(Request(URL, headers={"Range": "bytes=0-0", "Accept-Encoding": "identity"}), timeout=30) as response:
            content_range = response.headers.get("Content-Range", "")
            if response.status != 206 or not content_range.startswith("bytes 0-0/"):
                raise ValueError("Remote dataset does not support bounded range access")
            self.url = response.geturl()
            self.length = int(content_range.rsplit("/", 1)[1])
        self.position = 0
        self.cache = {}
        self.transferred = 1

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=io.SEEK_SET):
        position = offset if whence == io.SEEK_SET else self.position + offset if whence == io.SEEK_CUR else self.length + offset
        if not 0 <= position <= self.length:
            raise ValueError("Remote ZIP seek outside archive")
        self.position = position
        return position

    def read(self, count=-1):
        count = self.length - self.position if count < 0 else min(count, self.length - self.position)
        if count > MAX_TRANSFER:
            raise ValueError("Remote ZIP read exceeds the sample transfer budget")
        result = bytearray()
        while len(result) < count:
            index, offset = divmod(self.position, BLOCK)
            if index not in self.cache:
                start = index * BLOCK
                end = min(start + BLOCK, self.length) - 1
                if self.transferred + end - start + 1 > MAX_TRANSFER:
                    raise ValueError("Remote ZIP transfer budget exhausted")
                request = Request(self.url, headers={"Range": f"bytes={start}-{end}", "Accept-Encoding": "identity"})
                with urlopen(request, timeout=30) as response:
                    if response.status != 206 or response.headers.get("Content-Range") != f"bytes {start}-{end}/{self.length}":
                        raise ValueError("Incorrect remote ZIP range")
                    data = response.read(end - start + 2)
                self.transferred += len(data)
                if len(data) != end - start + 1:
                    raise ValueError("Incomplete remote ZIP range")
                self.cache[index] = data
            data = self.cache[index]
            take = min(count - len(result), len(data) - offset)
            result.extend(data[offset:offset + take])
            self.position += take
        return bytes(result)


def main():
    output = Path("datasets/imagecas-x-sample")
    output.mkdir(parents=True, exist_ok=True)
    path = output / "1.coronary.nii.gz"
    if path.exists():
        if hashlib.sha256(path.read_bytes()).hexdigest() != EXPECTED_SHA256:
            raise ValueError("Existing ImageCAS-X v2 case 1 checksum mismatch")
        print("Existing ImageCAS-X v2 case 1 label verified")
        return
    with RemoteZip() as remote, zipfile.ZipFile(remote) as archive:
        labels = [entry for entry in archive.infolist() if entry.filename.endswith("/1.coronary.nii.gz") or entry.filename == "1.coronary.nii.gz"]
        if not labels:
            candidates = [entry.filename for entry in archive.infolist() if entry.filename.endswith(".nii.gz")]
            print("Case 1 label not present; first label paths:", candidates[:10])
            return
        entry = labels[0]
        if entry.file_size > MAX_TRANSFER:
            raise ValueError("Selected label is larger than the sample budget")
        data = archive.read(entry)  # zipfile validates the selected member's CRC.
        if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
            raise ValueError("Downloaded ImageCAS-X v2 case 1 checksum mismatch")
        path.write_bytes(data)
        manifest = {"record": "23562647", "version": "v2", "license": "CC BY 4.0", "member": entry.filename,
                    "bytes": len(data), "crc32": f"{entry.CRC:08x}", "sha256": hashlib.sha256(data).hexdigest(),
                    "transferred_bytes": remote.transferred, "archive_bytes": remote.length}
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
