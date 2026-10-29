# STC12C5A60S2 固件工程

完整接线、协议和验收状态见 [仓库首页 README](../../README.md)。

- 内部 RC 22.1184 MHz；两路串口均为 115200、8 位数据、无校验、1 位停止位。UART1 接主板，UART2 接上位机。
- START1～4 核对 X1～X4（P3.2～P3.5），唯一有效且匹配才发送 `test\r\n`。有效 START 独立工作，不要求触发 X5，历史模号断开不拦截新的有效命令。
- X5（P3.6）为按键，高／低分别稳定 20 ms，只在空闲高→低时确认唯一模号并启动一次。上电按住不启动，须先松开再按。
- 按下后松开继续接收；长按不重复启动。测试忙时按下忽略、不排队，即使按住跨过 end 也不启动下一轮。
- 活动模号断开、改号或多路低时一次 ERR 并停测，接回不自动恢复；再次有效按键或新的匹配 START 才能恢复。正常 end 结束上传及旧模号监测。
- RD_NUM 按当前输入返回 `{"Result":"x"}\r\n`（x 为 1～4），输入无效返回 ERR；查询不启动会话。
- 每条完整主板记录上传一条 JSON，非空值只输出首组 `测点=实测值 单位:结果`。不设置首帧或帧间超时。
- Control 负责五路 GPIO 配置和快照；App 负责按键消抖、命令、会话与转换；System 负责串口和 Timer0 约 1 ms 消抖时基。Timer0 使用内部计时，不占用 P3.4，不启用外部中断。
- Public_cfg.h 集中公共类型和存储区宏；board_config.h 保存固定时钟、容量及 20 ms 消抖配置；原厂 MCU 头文件保持原样。

打开 Project.uvproj，构建唯一目标 **828C Protocol Conversion Project**；EIDE 使用同名目标。内部 RC 在 STC-ISP 中设为 22.1184 MHz。

也可在仓库根目录运行：

```text
python "04 测试验证/build_keil.py"
```

成功构建的 HEX 为 Objects/Bridge828C.hex，EIDE 输出根目录同为 Objects。构建中间文件和自动日志不纳入 Git，当前程序和 HEX 保留本地。

历史版本曾通过 **152 项主机测试**，Keil／µVision 均零错误、零警告；data=126、xdata=884、code=5233 字节，静态栈余量 131 字节。2026-10-29 按要求移除电脑模拟支持及配套测试脚本，当前仅保留 MCU 代码、Keil 构建和实板验证资料。历史测试不代表本次重新执行的结果，变更及构建结果见 [程序编写验证记录](../../04%20测试验证/程序编写验证记录.md)。

## 文件夹与文件说明

以下说明基于 2026-10-28 的目录检查，并于 2026-10-29 更新电脑模拟移除后的源码职责。Objects 中的产物和 Hardware 空目录属于本地状态，不代表 GitHub 必然存在这些内容。

### 各文件夹职责

| 文件夹 | 主要内容 | 职责 |
| --- | --- | --- |
| User | main.c、STARTUP.A51 | 程序启动和主循环。启动汇编关闭中断、清理内部 RAM、设置堆栈并进入 C 运行环境；main 依次初始化系统、输入及应用，再打开总中断并循环调用 AppComm_Poll。 |
| App | app_comm.c、app_comm.h | 核心业务层。处理上位机命令、X5 消抖、测试会话、主板记录解析、JSON 流式输出和异常恢复；头文件公开应用接口、状态及调试错误码。 |
| Control | input_control.c、input_control.h | 输入采集层。配置 X1～X5（P3.2～P3.6），一次读取端口形成输入快照，并从 X1～X4 识别唯一有效模号。按键消抖由 App 完成。 |
| System | System.c/.h、Uart1.c/.h、Uart2.c/.h | 底层驱动层。负责时钟分频、双串口初始化、接收环形缓冲、溢出检测、非阻塞发送，以及 Timer0 约 1 ms 消抖时基。 |
| Public | Public_cfg.h、board_config.h、STC12C5A60S2.h | 公共类型、存储区宏、容量与消抖配置、MCU 寄存器定义。 |
| Hardware | 本地为空 | 尚未承载驱动文件，也未作为源码分组加入工程。现有底层代码位于 System 和 Control。 |
| Objects | HEX、OBJ、LST、M51、构建日志及 EIDE 目标子目录 | 构建输出，可重新生成。实际内容随构建方式和时间变化，由 .gitignore 排除。 |
| Listings | 本地仅有 .gitkeep | 占位目录；Keil 当前的 ListingPath 指向 Objects，列表文件实际生成在那里。 |
| .eide | eide.yml、files.options.yml | EIDE 工程目标、源码分组、包含路径、Keil C51 工具链及编译选项；files.options.yml 当前没有逐文件附加参数。 |
| .vscode | tasks.json | VS Code 的 build、flash、build and flash、rebuild、clean 任务入口，调用 EIDE 对应命令。 |

Git 不保存空目录，因此本地为空的 Hardware 不一定显示在仓库网页中；占位文件所在目录是否上传仍受忽略规则约束。

### 程序运行关系

```text
User/STARTUP.A51
  └─ C 运行环境 → User/main.c
       ├─ Sys_Init()           System：串口、时钟分频、Timer0
       ├─ InputControl_Init()  Control：X1～X5 输入
       ├─ AppComm_Init()       App：协议与会话状态
       └─ EA = 1；循环 AppComm_Poll()
            ├─ 输入快照、模号监测与 X5 消抖
            ├─ UART2 接收 (RD_NUM)、(START,1)～(START,4)
            ├─ UART1 非阻塞发送 test\r\n
            └─ UART1 接收主板记录 → 校验、转换 → UART2 输出 JSON
```

App 的四种会话状态为 APP_IDLE（空闲）、APP_SENDING_START（发送启动）、APP_WAIT_FIRST（等待首条记录）、APP_RECEIVING（接收记录）。没有首帧或帧间超时。主板正常 end 结束会话；首条有效记录前直接 end 则按格式错误处理。

UART1 使用 P3.0 接收、P3.1 发送，连接主板；UART2 使用 P1.2 接收、P1.3 发送，连接上位机。中断负责字节收发和缓冲，主循环负责协议处理。App 直接从接收环形区流式生成 JSON，随编码释放数据，不另建完整 JSON 缓冲，也不保存整轮测试结果。

### 公共配置

- Public_cfg.h 定义 C51 的 uint8、uint16，以及 XDATA、IDATA、CODE 存储区宏，并包含原厂 MCU 寄存器定义。
- board_config.h 集中配置：时钟 22.1184 MHz、UART1 接收数组 768 字节、UART2 接收数组 64 字节、主板单行上限 511 字节、名称上限 31 字节、单位上限 8 字节、命令上限 23 字节、X5 消抖 20 ms。
- 环形缓冲各保留一个空槽，实际可存放量为 UART1 767 字节、UART2 63 字节。
- Control 直接访问 P3、P3M0、P3M1；串口驱动直接访问硬件寄存器并使用真实中断入口。电脑模拟变量、接收注入接口和条件编译分支已移除。

### Objects 编译产物

| 文件或类型 | 用途 |
| --- | --- |
| Bridge828C.hex | Keil 构建生成的烧录固件。 |
| Bridge828C（无扩展名） | Keil 链接生成的程序文件，含调试信息。 |
| *.obj | 各源码编译或汇编后的目标文件。 |
| *.lst | 编译、汇编列表，供检查生成代码。 |
| Bridge828C.m51 | 链接映射，包含内存分配、符号及调用关系等信息。 |
| Bridge828C.lnp | 链接器输入参数。 |
| Bridge828C.build_log.htm | µVision 构建日志。 |
| 828C Protocol Conversion Project/ | EIDE 目标目录，含 builder.params、compile_commands.json、ref.json、.lock 等辅助文件。 |

已有构建记录为 0 错误、0 警告，data=126、xdata=884、code=5233 字节。本次移除电脑模拟的构建结果见验证记录，实板验收仍需实际执行。

### 根目录文件

| 文件 | 用途 |
| --- | --- |
| Project.uvproj | Keil 主工程，包含唯一目标 828C Protocol Conversion Project、源码分组、包含路径和输出配置。 |
| Project.uvopt | Keil 调试与工程选项。 |
| Project.uvgui.Administrator | 本机 Keil 界面布局、窗口等个人状态，不属于固件逻辑。 |
| Project.code-workspace | VS Code 工作区入口。 |
| .clang-format | 四空格缩进、独立花括号等代码格式规则。 |
| .gitignore | 排除构建产物及部分本机 IDE 配置。 |
| README.md | 当前固件行为、构建方法和目录说明。 |
| STC12C5A60S2-35I-PDIP40.PDF | 本地芯片资料；版本管理中的数据手册位置以仓库根目录说明为准。本次未展开分析 PDF 正文。 |

### 配置检查与维护入口

1. **时钟一致性**：Project.uvproj 的 Cpu 字段含 CLOCK(45000000)，源码与串口、Timer0 设置按 22.1184 MHz 工作。该字段不能代替 ISP 对内部 RC 的设置；运行和仿真时应分别核对相应时钟配置。
2. **本机 EIDE 差异**：本次检查发现本地 .eide/eide.yml 增加了 D:/Keil_v5/C51/INC/STC 路径，并将 linker.misc-controls 清空；仓库原配置保留 RAMSIZE(256)、XDATA(0000H-03FFH)、CODE(0000H-0EFFFH) 边界。这些已有本机改动未随本次文档更新提交。换电脑或使用本机配置构建时，需要核对工具链路径和链接内存边界。
3. **烧录入口**：tasks.json 提供烧录任务，但当前 EIDE 自定义上传器的命令为空，使用前需配置实际烧录工具和参数。
4. **阅读和修改顺序**：建议先读 User，再看 Public、Control、System，最后阅读 App。改协议、JSON 格式或测试流程主要看 App；改输入映射看 Control；改串口和计时看 System；改容量与消抖参数看 Public/board_config.h。
