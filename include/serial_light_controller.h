/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SERIAL_LIGHT_CONTROLLER_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SERIAL_LIGHT_CONTROLLER_H_

#include <cstddef>
#include <string>

/**
 * @brief USB 串口报警灯控制器。
 *
 * 设备由系统 udev 初始化为 /dev/ch341-light，本类只负责按 SOP 告警状态发送 5 字节控制帧。
 */
class SerialLightController {
 public:
  explicit SerialLightController(std::string device_path = "/dev/ch341-light", int baud_rate = 9600);
  ~SerialLightController();

  SerialLightController(const SerialLightController&) = delete;
  SerialLightController& operator=(const SerialLightController&) = delete;

  bool Open();
  void Close();

  bool SetAlert(bool active);
  bool TurnRedFlashBeep();
  bool TurnOff();

  bool is_open() const;
  const std::string& device_path() const;

 private:
  bool ConfigureSerial() const;
  bool SendCommand(const unsigned char* command, std::size_t size) const;

  std::string device_path_;
  int baud_rate_ = 9600;
  int fd_ = -1;
  bool alert_active_ = false;
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SERIAL_LIGHT_CONTROLLER_H_
