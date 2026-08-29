# YOLOv8 INT8 Single Output Issue - 2026-08-27

## Symptom

On RK3588, the old INT8 YOLOv8 model can initialize and run, but object detection returns zero targets:

```text
models/yolov8.rknn
objects=0
```

In the measured run, 147 valid sampled frames produced no object detections.

## Confirmed Model Attributes

The old INT8 model reports:

```text
RKNN YOLOv8 初始化完成: models/yolov8.rknn, input=640x640, outputs=1
RKNN input attr: dims=1x640x640x3, fmt=1, type=INT8, qnt=2, zp=-128, scale=0.00392157
RKNN output attr[0]: dims=1x11x8400, fmt=3, type=INT8, qnt=2, zp=-127, scale=2.50872
```

The project config uses 7 classes:

```text
cover_cloth,long_handle,manual,padding_board,small_red_lever,top_pad,vertical_support_bracket
```

So the single output width `11` equals `4 box fields + 7 class scores`.

## Root Cause

This model is a single-output INT8 YOLOv8 export. Bounding box coordinates and class probabilities are stored in the same output tensor:

```text
[cx, cy, w, h, class0, class1, ... class6]
```

Box values are in the model input coordinate range, roughly `0..640`. Class scores are probabilities, roughly `0..1`. Because both share one INT8 quantization scale, the output scale becomes large:

```text
scale=2.50872
```

That scale is suitable for box coordinates but too coarse for class probabilities. The class scores are quantized/dequantized into unusable values, usually below `detector.conf_threshold`, so post-processing filters out every candidate.

The code detects this risk in `src/yolov8_detector.cpp` and prints a warning when a single-output quantized YOLOv8 tensor has a large output scale.

## Timing Comparison

Same video input, same full pipeline, first frame excluded:

| Model | YOLO total median | YOLO NPU median | Process median | Result |
|---|---:|---:|---:|---|
| `models/ai_sop_best_fp16.rknn` | 60.65 ms | 39.36 ms | 79.24 ms | detects objects |
| `models/yolov8.rknn` INT8 | 32.29 ms | 23.37 ms | 49.81 ms | 0 objects |

INT8 is faster, but the current single-output INT8 model is not usable for detection.

## Required Fix

For the final fast version, export INT8 in a quantization-friendly output layout. Do not use a single tensor that mixes box coordinates and class scores.

Recommended output layout:

```text
box / class / score split by branch
```

For example, the known-good COCO80 RKNN layout uses 9 outputs:

```text
[box_0, cls_0, score_0, box_1, cls_1, score_1, box_2, cls_2, score_2]
```

This lets class/score tensors use their own `0..1` quantization range, while box tensors use their own coordinate/logit range.

Use the patched Rockchip Ultralytics exporter in this repository to create that layout from the original `.pt` weights:

```bash
python3 scripts/export_ai_sop_rknnopt_onnx.py \
  --weights models/ai_sop_best.pt \
  --output models/ai_sop_best_rknnopt.onnx

python3 scripts/export_ai_sop_rknn_int8.py \
  --onnx models/ai_sop_best_rknnopt.onnx \
  --output models/ai_sop_best_int8.rknn \
  --dataset models/calibration/dataset.txt
```

Then set:

```text
detector.model_path=models/ai_sop_best_int8.rknn
```

Do not quantize `models/ai_sop_best.onnx` directly unless it has first been exported with the RKNN-optimized split-output detection head.

## Current Working Fallback

Use the FP16 model for correctness:

```text
detector.model_path=models/ai_sop_best_fp16.rknn
detector.conf_threshold=0.25
```

The FP16 model reports:

```text
RKNN output attr[0]: dims=1x11x8400, fmt=3, type=FP16, qnt=2, zp=0, scale=1
```

This avoids the single-output INT8 quantization loss and detects targets.
