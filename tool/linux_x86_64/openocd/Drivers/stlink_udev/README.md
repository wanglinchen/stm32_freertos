# Linux：ST-Link / OpenOCD 设备访问（udev）

Windows 使用 `Drivers/stsw-link009_v3` 里的 WinUSB INF；**Linux 无同名驱动包**，靠内核 `usb` + **udev 规则** 给当前用户读写调试器节点。

## 规则来源

OpenOCD 自带 `openocd/contrib/60-openocd.rules`（含 ST Microelectronics `0483:` ST-Link 等 VID:PID）。

## 安装

在仓库根（或任意目录）：

```bash
bash tool/linux_x86_64/openocd/Drivers/stlink_udev/install_udev.sh
```

或：

```bash
python3 flash/flash.py install-stlink-driver
```

脚本会把规则装到 `/etc/udev/rules.d/99-stm32-openocd.rules` 并 `udevadm reload`。**需要 sudo。**

安装后请将用户加入 **plugdev**（或发行版等效组，如 **dialout**）：

```bash
sudo usermod -aG plugdev "$USER"
```

重新登录或 `newgrp plugdev` 后重新插拔 ST-Link USB。

## WSL2

USB 设备需 **usbipd** 绑定到 WSL；udev 在 WSL 内生效。无法在 WSL 看到设备时，先在 Windows 用 `flash/flash.py` 验证。