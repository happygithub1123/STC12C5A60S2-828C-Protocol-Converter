# 828C 协议转换验证

在项目根目录运行（Python 3，Windows）：

```text
python "04 测试验证/run_tests.py"
python "04 测试验证/build_keil.py"
```

主机测试需要 Visual Studio C++ 工具链，脚本通过 vswhere 查找；Keil 构建默认使用 D:/Keil_v5，可用 `--keil` 指定其他目录。频率实测后用 `--fosc <Hz>` 生成正式构建；默认命令只生成测试时钟 HEX。

## 自动验证范围

- 读取真实 `01 参考资料/数据新格式.txt`，独立 Python 参考转换与同一份 C 固件输出逐字节比较：28 条记录，Open 的 36 个测量值全部保留。
- UART 字节间隔模拟为 87 µs，另覆盖 50 µs 主循环调度；仅是流量模拟，不模拟 8051 指令执行耗时。
- START 1～4、MH 对应、RD_NUM、带/不带 CRLF 的 OK 确认、NG 后 OK 独立结果。
- 任意名称、数值精度、不同单位、空值/单值/多值、尾逗号、JSON 转义、UTF-8 名称和单位、非法 UTF-8 拒绝。
- 分段命令/记录、首帧 3 秒超时、16 位计时回绕、首帧后长间隔、新 START 等当前 JSON 完整发送后切换、end 结束。
- 格式错误、保留键冲突、名称/单位容量、511/512 字节边界、UART1/UART2 溢出和恢复、发送中溢出、150 条随机生成记录与环形缓冲回绕。
- 每条输出用 JSON 解析器检查语法、唯一键名和 CRLF，不接受部分 JSON。

产物：

| 文件 | 用途 |
| --- | --- |
| results/test_results.txt | 每项用例、UART1 原始发送十六进制、UART2 字符串、接收峰值及错误码 |
| results/sample_mapping.json | 28 条主板原始记录与期望 JSON 一一对应 |
| results/sample_expected.jsonl | 实际 CRLF 分帧的期望串口字节，可用于上位机回放 |
| results/host_build.log | MSVC 编译记录（警告视为错误） |
| results/uv4_build.log | µVision 工程直接重建结果 |
| ../02 软件源码/顺天祥协议转换/BuildTest/build.log | Keil 编译、链接、内存检查及 HEX 生成记录 |

`app_last_error` 仅供调试器查看：0 正常，1 命令错误，2 数据格式错误，3 单行超长，4 UART1 接收溢出，5 UART2 接收溢出，6 首帧超时。线上所有这些错误统一为 `{"Result":"ERR"}\r\n`。

## 实物验收步骤（待执行）

1. ISP 选择内部 RC，记录实测频率；填写 board_config.h 或向脚本传入该频率，确认 115200 计算误差及实际引脚波形。
2. 使用两个独立串口端口连接 UART1/UART2，115200 8N1；从 UART2 发 START1～4，核对 UART1 收到 test1～4 和 CRLF。
3. UART1 回放 sample_mapping 的输入，UART2 抓包与 sample_expected.jsonl 按字节比较。再回放新的名称、单位、顺序及数量，确认没有固定样本依赖。
4. 在实板上测量 3 秒首帧窗口；测分段输入、连续输出、处理中重启会话、溢出后再 START 恢复。用最坏实际主板发送节奏确认峰值缓冲和中断处理能力。
5. 连接真实 828C 上位机，确认无需 ACK 等待、连续 JSON 都显示、同项目多值分号内容全部显示；保存软件版本、配置、收发抓包和界面记录。

本目录自动测试通过表示软件解析与流量模型通过，不等同于实物双串口或真实上位机验收通过。未连接真实硬件，未执行烧录。
