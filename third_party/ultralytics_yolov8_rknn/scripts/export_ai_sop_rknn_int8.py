#!/usr/bin/env python3
"""Convert RKNN-optimized YOLOv8 ONNX to INT8 RKNN for RK3588."""

from __future__ import annotations

import argparse
from pathlib import Path

import onnx
from rknn.api import RKNN


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--onnx', default='models/ai_sop_best_rknnopt.onnx', help='RKNN-optimized multi-output ONNX.')
    parser.add_argument('--output', default='models/ai_sop_best_int8.rknn', help='Output INT8 RKNN path.')
    parser.add_argument('--dataset', default='models/calibration/dataset.txt', help='Calibration image list.')
    parser.add_argument('--target', default='rk3588', help='RKNN target platform.')
    return parser.parse_args()


def resolve(path: str) -> Path:
    p = Path(path).expanduser()
    return p if p.is_absolute() else Path.cwd() / p


def check_ret(ret: int, step: str) -> None:
    if ret != 0:
        raise RuntimeError(f'{step} failed: {ret}')


def main() -> int:
    args = parse_args()
    onnx_path = resolve(args.onnx)
    output_path = resolve(args.output)
    dataset_path = resolve(args.dataset)
    if not onnx_path.is_file():
        raise FileNotFoundError(onnx_path)
    if not dataset_path.is_file():
        raise FileNotFoundError(dataset_path)

    graph = onnx.load(str(onnx_path))
    output_count = len(graph.graph.output)
    if output_count < 3:
        raise RuntimeError(
            f'{onnx_path} has only {output_count} output(s). Refusing to quantize normal single-output YOLOv8 ONNX.'
        )
    print(f'ONNX outputs: {output_count}')
    for i, out in enumerate(graph.graph.output):
        dims = []
        for d in out.type.tensor_type.shape.dim:
            dims.append(str(d.dim_value or d.dim_param or '?'))
        print(f'  output[{i}] {out.name}: ' + 'x'.join(dims))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    rknn = RKNN(verbose=True)
    try:
        check_ret(rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]], target_platform=args.target), 'config')
        check_ret(rknn.load_onnx(model=str(onnx_path)), 'load_onnx')
        check_ret(rknn.build(do_quantization=True, dataset=str(dataset_path)), 'build')
        check_ret(rknn.export_rknn(str(output_path)), 'export_rknn')
    finally:
        rknn.release()
    print(f'RKNN INT8 exported: {output_path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
