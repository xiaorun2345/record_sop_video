#!/usr/bin/env python3
"""Export AI-SOP YOLOv8 weights to an RKNN-optimized ONNX layout.

Run this on the training/conversion machine with the patched Ultralytics
environment installed. The output ONNX has split YOLOv8 branches:
box / class / score_sum for each detection scale.
"""

import argparse
import os
import shutil
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--weights",
        default="models/ai_sop_best.pt",
        help="Path to the trained YOLOv8 .pt weights.",
    )
    parser.add_argument(
        "--output",
        default="models/ai_sop_best_rknnopt.onnx",
        help="Path for the RKNN-optimized ONNX model.",
    )
    parser.add_argument("--imgsz", type=int, default=640, help="Square export image size.")
    parser.add_argument("--device", default="cpu", help="Export device, for example cpu or 0.")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite --output if it already exists.")
    return parser.parse_args()


def main():
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    weights = (repo_root / args.weights).resolve() if not Path(args.weights).is_absolute() else Path(args.weights)
    output = (repo_root / args.output).resolve() if not Path(args.output).is_absolute() else Path(args.output)
    ultralytics_root = repo_root / "third_party" / "ultralytics_yolov8_rknn"

    if not weights.exists():
        print(f"weights not found: {weights}", file=sys.stderr)
        return 2
    if output.exists() and not args.overwrite:
        print(f"output already exists, pass --overwrite: {output}", file=sys.stderr)
        return 2
    if not ultralytics_root.exists():
        print(f"patched Ultralytics tree not found: {ultralytics_root}", file=sys.stderr)
        return 2

    sys.path.insert(0, str(ultralytics_root))
    os.chdir(ultralytics_root)

    from ultralytics.cfg import get_cfg
    from ultralytics.engine.exporter import export

    cfg = get_cfg(
        overrides={
            "model": str(weights),
            "format": "rknn",
            "imgsz": args.imgsz,
            "batch": 1,
            "device": args.device,
            "half": False,
            "int8": False,
            "dynamic": False,
            "simplify": False,
            "opset": 12,
        }
    )
    generated = Path(export(cfg)).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if generated != output:
        shutil.copyfile(generated, output)
    print(f"RKNN-optimized ONNX exported: {output}")


if __name__ == "__main__":
    raise SystemExit(main())
