/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "serial_light_controller.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr unsigned char kRedFlashBeep[] = {0xFF, 0x04, 0x02, 0x04, 0xAA};
constexpr unsigned char kAllOff[] = {0xFF, 0x01, 0x01, 0x01, 0xAA};

bool BaudRateToTermios(const int baud_rate, speed_t* speed) {
  if (speed == nullptr) {
    return false;
  }
  switch (baud_rate) {
    case 1200:
      *speed = B1200;
      return true;
    case 2400:
      *speed = B2400;
      return true;
    case 4800:
      *speed = B4800;
      return true;
    case 9600:
      *speed = B9600;
      return true;
    case 19200:
      *speed = B19200;
      return true;
    case 38400:
      *speed = B38400;
      return true;
    case 57600:
      *speed = B57600;
      return true;
    case 115200:
      *speed = B115200;
      return true;
    default:
      return false;
  }
}

}  // namespace

SerialLightController::SerialLightController(std::string device_path, const int baud_rate)
    : device_path_(std::move(device_path)), baud_rate_(baud_rate) {}

SerialLightController::~SerialLightController() {
  TurnOff();
  Close();
}

bool SerialLightController::Open() {
  if (fd_ >= 0) {
    return true;
  }

  fd_ = open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (fd_ < 0) {
    std::cerr << "打开串口报警灯失败: " << device_path_ << ", " << std::strerror(errno) << std::endl;
    return false;
  }

  if (!ConfigureSerial()) {
    Close();
    return false;
  }

  std::cout << "串口报警灯已打开: " << device_path_ << ", baud=" << baud_rate_ << std::endl;
  return true;
}

void SerialLightController::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  alert_active_ = false;
}

bool SerialLightController::SetAlert(const bool active) {
  if (active == alert_active_) {
    return true;
  }
  return active ? TurnRedFlashBeep() : TurnOff();
}

bool SerialLightController::TurnRedFlashBeep() {
  if (!Open()) {
    return false;
  }
  if (!SendCommand(kRedFlashBeep, sizeof(kRedFlashBeep))) {
    return false;
  }
  alert_active_ = true;
  return true;
}

bool SerialLightController::TurnOff() {
  if (fd_ < 0) {
    alert_active_ = false;
    return true;
  }
  const bool ok = SendCommand(kAllOff, sizeof(kAllOff));
  if (ok) {
    alert_active_ = false;
  }
  return ok;
}

bool SerialLightController::is_open() const { return fd_ >= 0; }

const std::string& SerialLightController::device_path() const { return device_path_; }

bool SerialLightController::ConfigureSerial() const {
  speed_t speed = B9600;
  if (!BaudRateToTermios(baud_rate_, &speed)) {
    std::cerr << "不支持的串口报警灯波特率: " << baud_rate_ << std::endl;
    return false;
  }

  termios tty {};
  if (tcgetattr(fd_, &tty) != 0) {
    std::cerr << "读取串口报警灯参数失败: " << std::strerror(errno) << std::endl;
    return false;
  }

  cfmakeraw(&tty);
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);

  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    std::cerr << "配置串口报警灯失败: " << std::strerror(errno) << std::endl;
    return false;
  }

  tcflush(fd_, TCIOFLUSH);
  return true;
}

bool SerialLightController::SendCommand(const unsigned char* command, const std::size_t size) const {
  const unsigned char* cursor = command;
  std::size_t remaining = size;

  while (remaining > 0) {
    const ssize_t written = write(fd_, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "发送串口报警灯命令失败: " << std::strerror(errno) << std::endl;
      return false;
    }
    cursor += written;
    remaining -= static_cast<std::size_t>(written);
  }

  if (tcdrain(fd_) != 0) {
    std::cerr << "等待串口报警灯发送完成失败: " << std::strerror(errno) << std::endl;
    return false;
  }

  return true;
}
