# tests/

可选的 host 侧案例与说明目录；**不参与**交叉编译与 Kconfig/Makefile 扫描。固件行为与构建契约由 `build/build.py verify` 与 `gate-selftest` 覆盖。

- DTS 解析单测：`python tests/test_dts.py`
