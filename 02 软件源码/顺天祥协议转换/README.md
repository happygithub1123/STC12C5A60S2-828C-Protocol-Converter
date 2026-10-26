# STC12C5A60S2 固件工程

完整中文说明、接线、协议和验收状态见 [仓库首页 README](../../README.md)。

- 内部 RC：22.1184 MHz；UART1 接主板，UART2 接上位机，两路均 115200、8N1。
- START1～4 对应 X1～X4（P3.2～P3.5），1 秒内低电平有效才向主板发送一次 `test\r\n`。
- 每条主板记录上传一条 JSON；非空测量项仅保留首组，格式为 `测点=实测值 单位:结果`。
- Control 只负责输入配置和读取；App 负责等待、超时、命令和转换；System 负责双串口及通信计时。
- Public_cfg.h 集中公共类型和存储区宏，board_config.h 保存时钟与容量配置；原厂 MCU 头文件保持不变。

打开 Project.uvproj，构建唯一目标 **828C INTERNAL RC 22.1184MHz**；EIDE 使用同名目标。内部 RC 在 STC-ISP 中设置，须与板级配置一致。

也可从仓库根目录执行：

```text
python "04 测试验证/build_keil.py"
python "04 测试验证/run_tests.py"
```

构建成功后 HEX 位于 BuildMeasured/Bridge828C.hex，构建和测试产物不上传 Git。不要使用旧 Objects、Listings、BuildTest 中残留的旧固件。

本轮 75 项主机测试通过，Keil 与 µVision 零错误、零警告。新输入触发和首组输出格式仍待实板及真实上位机验收，详见 [程序编写验证记录](../../04%20测试验证/程序编写验证记录.md)。
