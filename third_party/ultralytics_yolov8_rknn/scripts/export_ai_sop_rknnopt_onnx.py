#!/usr/bin/env python3
"""Export AI-SOP YOLOv8 PT to RKNN-optimized multi-output ONNX."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

PATCHED_ULTRALYTICS = Path('/home/user/toolchains/ultralytics_yolov8_rknn')
if str(PATCHED_ULTRALYTICS) not in sys.path:
    sys.path.insert(0, str(PATCHED_ULTRALYTICS))

from ultralytics import YOLO  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--weights', default='models/ai_sop_best.pt', help='Source YOLOv8 .pt weights.')
    parser.add_argument('--output', default='models/ai_sop_best_rknnopt.onnx', help='Output RKNN-optimized ONNX path.')
    parser.add_argument('--imgsz', type=int, default=640, help='Static export input size.')
    return parser.parse_args()


def resolve(path: str) -> Path:
    p = Path(path).expanduser()
    return p if p.is_absolute() else Path.cwd() / p


def main() -> int:
    args = parse_args()
    weights = resolve(args.weights)
    output = resolve(args.output)
    if not weights.is_file():
        raise FileNotFoundError(weights)

    model = YOLO(str(weights))
    exported = Path(model.export(format='rknn', imgsz=args.imgsz, dynamic=False))
    if not exported.is_absolute():
        exported = Path.cwd() / exported
    if not exported.is_file():
        candidate = weights.with_suffix('.onnx')
        if candidate.is_file():
            exported = candidate
        else:
            raise FileNotFoundError(f'RKNN optimized ONNX was not created: {exported}')

    output.parent.mkdir(parents=True, exist_ok=True)
    if exported.resolve() != output.resolve():
        shutil.copy2(exported, output)
    print(f'RKNN optimized ONNX exported: {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
