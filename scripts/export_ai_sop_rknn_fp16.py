#!/usr/bin/env python3
"""Export AI-SOP YOLOv8 ONNX to a non-quantized RKNN model for RK3588.

Run this on the model conversion machine where RKNN Toolkit2 is installed.
The RK3588 board runtime is enough for inference, but not for ONNX->RKNN export.
"""

import argparse
import sys

from rknn.api import RKNN


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--onnx",
        default="models/ai_sop_best.onnx",
        help="Path to the YOLOv8 ONNX model.",
    )
    parser.add_argument(
        "--output",
        default="models/ai_sop_best_fp16.rknn",
        help="Path for the exported non-quantized RKNN model.",
    )
    parser.add_argument(
        "--target",
        default="rk3588",
        help="RKNN target platform.",
    )
    return parser.parse_args()


def check(ret, step):
    if ret != 0:
        print(f"{step} failed: {ret}", file=sys.stderr)
        sys.exit(ret)


def main():
    args = parse_args()
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
        check(rknn.load_onnx(model=args.onnx), "load_onnx")
        check(rknn.build(do_quantization=False), "build")
        check(rknn.export_rknn(args.output), "export_rknn")
    finally:
        rknn.release()


if __name__ == "__main__":
    main()
