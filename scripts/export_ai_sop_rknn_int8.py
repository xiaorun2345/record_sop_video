#!/usr/bin/env python3
"""Convert an RKNN-optimized AI-SOP YOLOv8 ONNX model to INT8 RKNN."""

import argparse
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--onnx",
        default="models/ai_sop_best_rknnopt.onnx",
        help="Path to the RKNN-optimized ONNX model.",
    )
    parser.add_argument(
        "--output",
        default="models/ai_sop_best_int8.rknn",
        help="Path for the exported INT8 RKNN model.",
    )
    parser.add_argument(
        "--dataset",
        default="models/calibration/dataset.txt",
        help="RKNN calibration dataset file.",
    )
    parser.add_argument("--target", default="rk3588", help="RKNN target platform.")
    parser.add_argument(
        "--allow-unoptimized-onnx",
        action="store_true",
        help="Allow ONNX filenames that do not contain 'rknnopt'.",
    )
    return parser.parse_args()


def check(ret, step):
    if ret != 0:
        print(f"{step} failed: {ret}", file=sys.stderr)
        sys.exit(ret)


def resolve_path(repo_root, value):
    path = Path(value)
    return path if path.is_absolute() else repo_root / path


def make_resolved_dataset(dataset_path, output_path):
    calibration_dir = dataset_path.parent
    resolved_lines = []
    missing = []
    for raw_line in dataset_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        image_path = Path(line)
        if image_path.exists():
            resolved_lines.append(str(image_path.resolve()))
            continue
        local_match = calibration_dir / image_path.name
        if local_match.exists():
            resolved_lines.append(str(local_match.resolve()))
            continue
        missing.append(line)

    if missing:
        preview = "\n".join(missing[:5])
        raise FileNotFoundError(f"calibration images not found:\n{preview}")
    if not resolved_lines:
        raise ValueError(f"empty calibration dataset: {dataset_path}")

    resolved_dataset = output_path.with_suffix(".dataset.txt")
    resolved_dataset.write_text("\n".join(resolved_lines) + "\n", encoding="utf-8")
    return resolved_dataset


def main():
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    onnx_path = resolve_path(repo_root, args.onnx).resolve()
    output_path = resolve_path(repo_root, args.output).resolve()
    dataset_path = resolve_path(repo_root, args.dataset).resolve()

    if not onnx_path.exists():
        print(f"ONNX not found: {onnx_path}", file=sys.stderr)
        return 2
    if not dataset_path.exists():
        print(f"dataset not found: {dataset_path}", file=sys.stderr)
        return 2
    if "rknnopt" not in onnx_path.name and not args.allow_unoptimized_onnx:
        print(
            "refusing to quantize a likely single-output ONNX. "
            "Use export_ai_sop_rknnopt_onnx.py first, or pass --allow-unoptimized-onnx.",
            file=sys.stderr,
        )
        return 2

    output_path.parent.mkdir(parents=True, exist_ok=True)
    resolved_dataset = make_resolved_dataset(dataset_path, output_path)

    from rknn.api import RKNN

    rknn = RKNN(verbose=True)
    try:
        check(
            rknn.config(
                mean_values=[[0, 0, 0]],
                std_values=[[255, 255, 255]],
                target_platform=args.target,
            ),
            "config",
        )
        check(rknn.load_onnx(model=str(onnx_path)), "load_onnx")
        check(rknn.build(do_quantization=True, dataset=str(resolved_dataset)), "build")
        check(rknn.export_rknn(str(output_path)), "export_rknn")
    finally:
        rknn.release()

    print(f"INT8 RKNN exported: {output_path}")
    print(f"resolved calibration dataset: {resolved_dataset}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
