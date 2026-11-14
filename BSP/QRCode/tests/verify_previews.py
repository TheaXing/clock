"""Decode real page rasters independently and export lossless previews."""
import argparse
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument("raster_dir", type=Path)
parser.add_argument("output_dir", type=Path)
parser.add_argument("--dependencies", type=Path)
args = parser.parse_args()
if args.dependencies:
    sys.path.insert(0, str(args.dependencies.resolve()))

from PIL import Image
import zxingcpp

expected = {
    "wifi-step1": "WIFI:T:WPA;S:Clock-Setup-1234ABCD;P:0123456789ABCDEF;;",
    "wifi-step2": "http://192.168.4.1/setup",
    "wifi-connecting": None,
    "wifi-module-error": None,
    "wifi-random-error": None,
    "wifi-saved": None,
}
args.output_dir.mkdir(parents=True, exist_ok=True)
for name, text in expected.items():
    image = Image.open(args.raster_dir / f"{name}.ppm")
    assert image.size == (240, 320)
    decoded = zxingcpp.read_barcodes(image)
    actual = [result.text for result in decoded]
    assert actual == ([text] if text is not None else []), (name, actual)
    image.save(args.output_dir / f"{name}.png")
    print(f"{name}: independent decode {'passed' if text else 'no QR, as expected'}")
