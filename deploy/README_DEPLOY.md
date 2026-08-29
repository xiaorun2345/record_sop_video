# RK3588 SOP Runtime Package

## First Run On Target Device

```bash
tar -xzf rk3588_sop_runtime.tar.gz
cd rk3588_sop_runtime
./install_permissions.sh
./check_runtime.sh
```

Unplug and replug the Orbbec camera after installing udev rules.

## Run SOP

```bash
./run_sop.sh
```

## Run 1080p Hardware Recorder

```bash
./run_recorder.sh
```

Press `S` to start recording, `Q` to stop recording, and `Esc` to exit.

For automated smoke tests:

```bash
./run_recorder.sh --record-seconds 3
```

## Target System Requirements

The package includes the application binaries, RKNN runtime library, Orbbec SDK libraries, models, and config files.

The target device still needs a compatible RK3588 Linux image with:

- RKNPU kernel driver and `/dev/dri/renderD129`
- OpenCV 4.5 runtime libraries
- GStreamer 1.18 runtime
- Rockchip GStreamer MPP plugin with `mpph264enc`
