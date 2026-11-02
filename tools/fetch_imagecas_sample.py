"""Fetch only case 1 from the public ImageCAS split archive, with bounded I/O.

Kaggle serves each split volume inside a ZIP wrapper. Both ZIP layers are
streamed; complete selected members are checked against their local-header CRC.
No archive directory extraction or untrusted pathname writes are performed.
"""
import binascii
import hashlib
import json
import os
from pathlib import Path
import struct
from urllib.request import Request, urlopen
from urllib.error import URLError
from http.client import IncompleteRead
import zlib
from concurrent.futures import ThreadPoolExecutor


LIMIT = 192 * 1024 * 1024
BLOCK = 1024 * 1024
URL = "https://www.kaggle.com/api/v1/datasets/download/xiaoweixumedicalai/imagecas/1-200.z01?datasetVersionNumber=3"
HEADER = struct.Struct("<4s5H3I2H")


class RangeReader:
    """Small public HTTP ranges with bounded prefetch and a resumable cache."""
    def __init__(self):
        request = Request(URL, headers={"Range": "bytes=0-0", "Accept-Encoding": "identity"})
        with urlopen(request, timeout=30) as response:
            content_range = response.headers.get("Content-Range", "")
            if response.status != 206 or not content_range.startswith("bytes 0-0/"):
                raise ValueError("Server did not honour a one-byte range probe")
            self.url = response.geturl()  # Public signed download URL; never persisted or printed.
            self.total = int(content_range.rsplit("/", 1)[1])
            cache_key = hashlib.sha256((str(self.total) + response.headers.get("ETag", "")).encode()).hexdigest()[:16]
        self.cache = Path("build/dataset-metadata/imagecas-chunks") / cache_key
        self.cache.mkdir(parents=True, exist_ok=True)
        self.position = 0
        self.chunk_size = 2 * BLOCK
        self.pool = ThreadPoolExecutor(max_workers=6)
        self.futures = {}
        self._prefetch(0)

    def _fetch(self, index):
        start = index * self.chunk_size
        end = min(start + self.chunk_size, LIMIT) - 1
        path = self.cache / f"{index:04d}.bin"
        if path.exists() and path.stat().st_size == end - start + 1:
            return path.read_bytes()
        request = Request(self.url, headers={"Range": f"bytes={start}-{end}", "Accept-Encoding": "identity"})
        for attempt in range(3):
            try:
                with urlopen(request, timeout=60) as response:
                    expected = f"bytes {start}-{end}/{self.total}"
                    if response.status != 206 or response.headers.get("Content-Range") != expected:
                        raise ValueError("Server returned an incorrect archive range")
                    body = response.read(end - start + 2)
                break
            except (TimeoutError, URLError, ConnectionError, IncompleteRead):
                if attempt == 2:
                    raise
                print(f"Retrying archive chunk {index} ({attempt + 1}/2)", flush=True)
        if len(body) != end - start + 1:
            raise ValueError("Incomplete archive range")
        partial = path.with_suffix(".partial")
        partial.write_bytes(body)
        os.replace(partial, path)
        return body

    def _prefetch(self, current):
        for index in range(current, min(current + 6, LIMIT // self.chunk_size)):
            if index not in self.futures:
                self.futures[index] = self.pool.submit(self._fetch, index)

    def read(self, count):
        count = min(count, LIMIT - self.position)
        result = bytearray()
        while len(result) < count:
            index, offset = divmod(self.position, self.chunk_size)
            self._prefetch(index)
            body = self.futures[index].result()
            take = min(count - len(result), len(body) - offset)
            if take <= 0:
                break
            result.extend(body[offset:offset + take])
            self.position += take
        return bytes(result)

    def close(self):
        for future in self.futures.values():
            future.cancel()
        self.pool.shutdown(wait=True, cancel_futures=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def exact(stream, count):
    result = bytearray()
    while len(result) < count:
        block = stream.read(count - len(result))
        if not block:
            raise EOFError("Archive prefix ended before selected member completed")
        result.extend(block)
    return bytes(result)


class OuterZipStream:
    def __init__(self, response):
        self.response = response
        header = HEADER.unpack(exact(response, HEADER.size))
        if header[0] != b"PK\x03\x04" or header[3] != 8 or header[2] & 1:
            raise ValueError("Unexpected public ZIP wrapper")
        name = exact(response, header[-2]).decode("utf-8")
        exact(response, header[-1])
        if name != "1-200.z01":
            raise ValueError("Unexpected split-volume name")
        self.decoder = zlib.decompressobj(-15)
        self.pending = bytearray()
        self.received = HEADER.size + header[-2] + header[-1]
        self.reported = 0

    def read(self, count):
        while len(self.pending) < count:
            block = self.response.read(min(BLOCK, LIMIT - self.received))
            if not block:
                break
            self.received += len(block)
            self.pending.extend(self.decoder.decompress(block, 2 * BLOCK))
            if self.decoder.unconsumed_tail:
                raise ValueError("Unexpected expansion ratio in ZIP wrapper")
            progress = self.received // (16 * BLOCK)
            if progress > self.reported:
                print(f"Downloaded archive prefix: {self.received / BLOCK:.1f} MiB", flush=True)
                self.reported = progress
            if self.received >= LIMIT and len(self.pending) < count:
                raise ValueError("Bounded sample download limit reached")
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result


def main():
    directory = Path("datasets/imagecas-sample")
    directory.mkdir(parents=True, exist_ok=True)
    manifest_path = directory / "manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        for entry in manifest["files"]:
            path = directory / entry["name"]
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                raise ValueError(f"Existing sample checksum mismatch: {path}")
        if len(manifest["files"]) == 2:
            print("Existing ImageCAS sample verified")
            return
    manifest = {
        "dataset": "xiaoweixumedicalai/imagecas",
        "dataset_version": 3,
        "source_part": "1-200.z01",
        "license": "CC BY-NC 4.0 (public Kaggle metadata)",
        "case_id": "1",
        "purpose": "development sample; not a held-out benchmark",
        "files": [],
    }
    with RangeReader() as response:
        stream = OuterZipStream(response)
        signature = exact(stream, 4)
        if signature == b"PK\x07\x08":  # Split archive spanning marker.
            signature = exact(stream, 4)
        for _ in range(6):
            if signature != b"PK\x03\x04":
                raise ValueError("Unexpected inner ZIP signature")
            header = HEADER.unpack(signature + exact(stream, HEADER.size - 4))
            flags, method, expected_crc, compressed, uncompressed = header[2], header[3], header[6], header[7], header[8]
            name = exact(stream, header[-2]).decode("utf-8")
            exact(stream, header[-1])
            print(f"Member: {name} ({uncompressed:,} bytes)", flush=True)
            if flags & 9 or method not in (0, 8) or max(compressed, uncompressed) >= LIMIT:
                raise ValueError("Unsupported sample member encoding")
            if name.endswith("/"):
                if uncompressed or compressed > 16:
                    raise ValueError("Unexpected nonempty directory member")
                exact(stream, compressed)
                signature = exact(stream, 4)
                continue
            basename = name.rsplit("/", 1)[-1]
            if basename not in {"1.img.nii.gz", "1.label.nii.gz"}:
                print("Stopped before reading another case", flush=True)
                break
            path = directory / basename
            if path.exists():
                crc = size = 0
                digest = hashlib.sha256()
                with path.open("rb") as existing:
                    while block := existing.read(BLOCK):
                        size += len(block)
                        crc = binascii.crc32(block, crc)
                        digest.update(block)
                if size != uncompressed or crc != expected_crc:
                    raise ValueError(f"Existing sample fails archive integrity checks: {path}")
                remaining = compressed
                while remaining:
                    block = exact(stream, min(BLOCK, remaining))
                    remaining -= len(block)
                manifest["files"].append({"name": basename, "bytes": size, "crc32": f"{crc:08x}", "sha256": digest.hexdigest()})
                print(f"Reused verified {basename}", flush=True)
                signature = exact(stream, 4)
                continue
            partial = directory / (basename + ".partial")
            decoder = zlib.decompressobj(-15) if method == 8 else None
            crc = written = 0
            digest = hashlib.sha256()
            remaining = compressed
            with partial.open("wb") as output:
                while remaining:
                    block = exact(stream, min(BLOCK, remaining))
                    remaining -= len(block)
                    data = decoder.decompress(block, uncompressed - written + 1) if decoder else block
                    if decoder and decoder.unconsumed_tail:
                        raise ValueError("Member expands beyond advertised size")
                    written += len(data)
                    if written > uncompressed:
                        raise ValueError("Member exceeds advertised uncompressed size")
                    crc = binascii.crc32(data, crc)
                    digest.update(data)
                    output.write(data)
            if written != uncompressed or crc != expected_crc or (decoder and not decoder.eof):
                raise ValueError("Incomplete sample or failed CRC verification")
            os.replace(partial, path)
            manifest["files"].append({"name": basename, "bytes": written, "crc32": f"{crc:08x}", "sha256": digest.hexdigest()})
            manifest["complete"] = len(manifest["files"]) == 2
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
            print(f"Verified {basename}", flush=True)
            if len(manifest["files"]) == 2:
                break
            signature = exact(stream, 4)
        manifest["downloaded_prefix_bytes"] = stream.received
    if not manifest["files"]:
        raise ValueError("No complete sample obtained")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2), flush=True)


if __name__ == "__main__":
    main()
