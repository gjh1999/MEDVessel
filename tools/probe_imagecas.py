"""Read public ImageCAS metadata and test bounded HTTP range access."""
import json
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen


def main():
    root = Path("build/dataset-metadata")
    root.mkdir(parents=True, exist_ok=True)
    base = "https://www.kaggle.com/api/v1/datasets/"
    dataset = "xiaoweixumedicalai/imagecas"
    try:
        with urlopen(base + "view/" + dataset, timeout=20) as response:
            body = response.read(2_000_001)
        if len(body) > 2_000_000:
            raise ValueError("Metadata response exceeds expected size")
        metadata = json.loads(body)
        (root / "imagecas-view.json").write_bytes(body)
        print("Dataset metadata:", {k: v for k, v in metadata.items() if "licen" in k.lower() or k in {"title", "totalBytes", "isPrivate"}})
    except (HTTPError, ValueError) as error:
        print("Metadata unavailable:", str(error))
    url = base + "download/" + dataset + "/1-200.change2zip"
    request = Request(url, headers={"Range": "bytes=-65536", "Accept-Encoding": "identity"})
    try:
        with urlopen(request, timeout=30) as response:
            content_range = response.headers.get("Content-Range")
            content_type = response.headers.get("Content-Type")
            print("Archive range status:", response.status, "range:", content_range, "type:", content_type)
            # Never silently download the full multi-gigabyte archive.
            if response.status != 206 or not content_range:
                print("Stopped: server did not honour bounded range access")
                return
            body = response.read(65537)
            if len(body) > 65536:
                raise ValueError("Range response exceeds requested size")
            (root / "imagecas-tail.bin").write_bytes(body)
            print("Saved archive metadata bytes:", len(body))
    except HTTPError as error:
        print("Archive access denied:", error.code)


if __name__ == "__main__":
    main()
