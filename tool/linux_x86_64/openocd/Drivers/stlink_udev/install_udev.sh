#!/usr/bin/env bash
# 安装 OpenOCD udev 规则（含 ST-Link 0483:*）。需 root。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OCD_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RULES_SRC="${OCD_ROOT}/contrib/60-openocd.rules"
DEST="/etc/udev/rules.d/99-stm32-openocd.rules"

if [[ ! -f "${RULES_SRC}" ]]; then
  echo "找不到 ${RULES_SRC}，请先 python3 flash/flash.py install-openocd" >&2
  exit 1
fi

if [[ "$(id -u)" -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    exec sudo bash "${BASH_SOURCE[0]}" "$@"
  fi
  echo "需要 root 或 sudo 才能写入 ${DEST}" >&2
  exit 1
fi

install -m 0644 "${RULES_SRC}" "${DEST}"
udevadm control --reload-rules
udevadm trigger
echo "已安装 ${DEST}"
echo "若仍无权限，执行: usermod -aG plugdev <用户名> 后重新登录并拔插 USB"