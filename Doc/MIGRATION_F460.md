# MOTOR_APP_F460 迁移说明

## 1. 迁移结论

本目录是从 `MOTOR_APP/` 新建的 HC32F460 APP 工程，使用
`MOTOR_APP_XHCode_F460/Eclipse/` 的 F460 DDL、引脚、触发、DMA 和中断资源，
同时保留原 APP 的 Simulink 生成代码、控制流程、CAN/Modbus 协议和参数保存逻辑。

这不是把 XHCode 的示例 `main.c` 直接替换进产品工程。XHCode 生成的外设初始化已经按功能拆分到
`Hardware/`，原产品运行时逻辑仍由 `User/`、`Components/` 和 `Model/Code/Model_ert_rtw/` 负责。
原 `MOTOR_APP/` 未在本次迁移中修改。

当前结果是“源码静态迁移 + EIDE 完整编译通过”，还不是目标板验收结果。

## 2. 工程和构建边界

| 项目 | 当前值 |
|---|---|
| MCU | HC32F460 |
| 编译器 | EIDE GCC / `arm-none-eabi-gcc` |
| C 标准 | C11 |
| FPU | Cortex-M4 single precision, hard ABI |
| APP 链接地址 | `0x00010000` |
| APP 槽大小 | `0x30000`，即 192 KiB |
| RAM | `0x1FFF8000` 起始，188 KiB |
| APP 身份段 | `0x00011000`，48 字节 |
| 链接脚本 | `GCC/hc32f460-app.ld` |

F460 的片内 Flash 是 512 KiB，但本工程继续使用产品现有的 A/B 布局：

| 区域 | 地址范围 | 大小 |
|---|---:|---:|
| Loader | `0x00000000` - `0x0000FFFF` | 64 KiB |
| APP-A | `0x00010000` - `0x0003FFFF` | 192 KiB |
| APP-B | `0x00040000` - `0x0006FFFF` | 192 KiB |
| SAVE-A | `0x00070000` - `0x00073FFF` | 16 KiB |
| SAVE-B | `0x00074000` - `0x00077FFF` | 16 KiB |
| INFO | `0x00078000` - `0x0007FFFF` | 32 KiB |

`driver/src/hc32_ll_icg.c` 生成的 `.icg_sec` 位于芯片绝对地址 `0x400`，属于从零地址启动的
设备初始配置。F460 APP 链接在 `0x10000`，因此 `hc32f460-app.ld` 明确丢弃该段，避免把它
误放进 APP 槽。实际使用时，F460 的 Loader/烧录镜像必须负责正确的 ICG 配置；不能把现有
HC32F448 Loader 直接当成 F460 Loader 使用。

## 3. 时钟配置

文件：`Hardware/clock.c`。

| 参数 | 配置 |
|---|---|
| 外部晶振 | 8 MHz XTAL，OSC 模式，高驱动，稳定时间 2 ms |
| PLLM | `1`，寄存器值 `0` |
| PLLN | `50`，寄存器值 `49` |
| PLLP/Q/R | 均为 `2`，寄存器值均为 `1` |
| PLL 源 | XTAL |
| HCLK | DIV1 |
| EXCLK | DIV2 |
| PCLK0 | DIV1 |
| PCLK1 | DIV2 |
| PCLK2/PCLK3 | DIV4 |
| PCLK4 | DIV2 |
| Flash 等待周期 | `EFM_WAIT_CYCLE5` |
| GPIO 读等待 | `GPIO_RD_WAIT3` |

根据 F460 DDL 的公式 `8 MHz / 1 * 50 / 2`，PLL 系统时钟为 200 MHz；PCLK1 为 100 MHz。
该频率和 XHCode 生成的时钟参数一致，仍需在目标板上读取时钟寄存器确认晶振和板级时钟实际匹配。

## 4. 引脚映射

| 功能 | F460 引脚/资源 | 初始化文件 |
|---|---|---|
| TMR4 U 下桥臂 | PB13 / `TMR4_1-OUL` | `Hardware/timer4.c` |
| TMR4 V 下桥臂 | PB14 / `TMR4_1-OVL` | `Hardware/timer4.c` |
| TMR4 W 下桥臂 | PB15 / `TMR4_1-OWL` | `Hardware/timer4.c` |
| TMR4 U 上桥臂 | PA08 / `TMR4_1-OUH` | `Hardware/timer4.c` |
| TMR4 V 上桥臂 | PA09 / `TMR4_1-OVH` | `Hardware/timer4.c` |
| TMR4 W 上桥臂 | PA10 / `TMR4_1-OWH` | `Hardware/timer4.c` |
| 编码器 MOSI/SDA | PA00 / `SPI1-MOSI` | `Hardware/encoder.c` |
| 编码器 MISO/SDA 输入 | PA12 / `SPI1-MISO` | `Hardware/encoder.c` |
| 编码器 SCK | PA15 / `SPI1-SCK` | `Hardware/encoder.c` |
| 编码器 CS | PB06，软件 GPIO | `Hardware/encoder.c` |
| CAN TX | PB12 / `CAN-TXD` | `Hardware/can.c` |
| CAN RX | PB03 / `CAN-RXD` | `Hardware/can.c` |
| USART3 RX | PH02 | `Hardware/uart3.c` |
| USART3 TX | PC13 | `Hardware/uart3.c` |

ADC 引脚全部配置为模拟输入；PB03、PA12、PA15 与调试功能重叠时，`Hardware/gpio.c` 会关闭
对应的 TDI/SWO 调试复用。下载调试接口和新驱动板的实际复用关系仍需确认。

## 5. ADC 和同步采样

F448 原工程使用 ADC1/ADC2/ADC3；F460 没有可直接对应的 ADC3 资源，因此将 9 路信号重新
映射到 ADC1/ADC2。`Hardware/adc.h` 的宏是控制模型实际读取的最终来源。

| 模型信号 | ADC 资源 | F460 引脚 |
|---|---|---|
| `IU_ADC` | ADC2 CH3 / `DR3` | PB0 |
| `IV_ADC` | ADC2 CH0 / `DR0` | PA7 |
| `IW_ADC` | ADC1 CH0 / `DR0` | PA6 |
| `HALL2_ADC` | ADC1 CH1 / `DR1` | PA3 |
| `HALL1_ADC` | ADC2 CH1 / `DR1` | PA4 |
| `PWR_24V_DET` | ADC1 CH2 / `DR2` | PA1 |
| `PWR_5V_DET` | ADC1 CH3 / `DR3` | PA2 |
| `MOTOR_TEMP` | ADC2 CH2 / `DR2` | PB1 |
| `MOS_TEMP` | ADC2 CH4 / `DR4` | PA5 |

ADC 参数：

- ADC1、ADC2：12 bit、右对齐、序列 A 单次扫描。
- ADC1 序列 A：CH0、CH1、CH2、CH3。
- ADC2 序列 A：CH0、CH1、CH2、CH3、CH4。
- ADC1 序列 A 由 `TMR4_1_SCMP4` 经 `AOS_ADC1_0` 触发。
- ADC1/ADC2 使用 `ADC_SYNC_ADC1_ADC2`、`ADC_SYNC_SINGLE_PARALLEL_TRIG`，触发延迟为 1。
- ADC1 的 EOCA 是产品快速控制循环的唯一 NVIC 入口，映射到 F460 `INT116_IRQn`，优先级 00。

XHCode 还生成了 ADC1 CH5/CH8/CH9 的 remap，但没有把这些通道加入序列 A；这些 remap 不会
改变当前 9 路模型采样。它们是生成配置残留，后续若要精简配置可以删除，但必须同步确认
XHCode 工具是否依赖这些 remap 项。

### 过流硬件监视

F460 的 AWD 资源按通道位图工作：

- ADC1 AWD0 监视 CH0，即 `IW`。
- ADC2 AWD0 监视 CH0，即 `IV`，并追加选择 CH3，即 `IU`。
- ADC1、ADC2 的 AWD0 均使能。
- 仅 ADC1 使能 AWD 序列 A 中断源，并由 ADC1 EOCA/共享入口处理。
- `User/motor.c` 在 `ADC1_Handler()` 中读取 ADC1 CH0、ADC2 CH0、ADC2 CH3 的 AWD 标志。
- 任一路超限都会设置 `HardOverCurrent` 并关闭 PWM 输出；ADC2 没有单独打开 NVIC 入口。

这保持了原工程“ADC1 高速中断统一处理 ADC1/2/3 过流标志”的软件语义，但 F460 上的第三相
已变成 ADC2 CH3。AWD 阈值仍沿用原工程的数值，正式使用前必须依据 F460 参考电压、采样
放大器和新驱动板重新测量电压到电流的换算关系。

## 6. TMR4 PWM

文件：`Hardware/timer4.c`、`Hardware/timer4.h`。

| 参数 | 配置 |
|---|---|
| 外设 | `TMR4_1` |
| 计数源 | 内部时钟，DIV1 |
| 计数模式 | Triangle，中心对齐 |
| 周期寄存器 | `PWM_TIM_CNT = 5000` |
| 死区 | `PWM_DEADTIME = 50`，每相 PDAR/PDBR 均设置 |
| 初始比较值 | 0 |
| 比较值/比较模式缓冲 | Valley |
| 缓冲联动 | 比较值和比较模式在 valley 更新 |
| ADC 触发事件 | SCMP4，比较值 `4900`，向上计数匹配 |
| 触发事件通道 | `TMR4_EVT_CH_WH` |
| 异常输出 | 六路 PWM 均配置为 Hi-Z |

上、下桥输出动作表与 XHCode 生成值一致，低桥使用扩展控制位。启动时先写入安全初值、
启动计数器，再通过 `OCSRU/V/W` 清除输出使能；模型运行时仍由 `PWMOUT_ENABLE()`/
`PWMOUT_DISABLE()` 控制三相输出。

## 7. 编码器 SPI + DMA

SPI1 配置为 3-wire、全双工、主机、Mode 3、8 bit、MSB first、PCLK1/16。按当前时钟树
计算 SCK 约为 6.25 MHz。间隔、释放、建立延时均显式设置为 1 SCK，环回关闭，SPI 奇偶校验
自诊断关闭。

DMA 资源：

- DMA2 CH0：SPI1 SPRI，固定 SPI 数据寄存器到递增接收缓冲区。
- DMA2 CH1：SPI1 SPTI，递增发送缓冲区到固定 SPI 数据寄存器。
- 两个 SPI DMA 通道不启用独立 DMA 中断。
- `TMR4_1` valley/UDF 中断启动一次 4 字节编码器帧；该中断映射到 `INT092_IRQn`，优先级 02。
- ADC1 快速中断调用 `encoder_recv()`，检查 DMA 完成、释放 CS、校验 CRC 并更新角度。

DMA 地址、帧长度和编码器协议是用户运行时适配，不是 XHCode 图形界面自动生成的业务逻辑。

## 8. USART3 + DMA

USART3 为 921600 baud、8 数据位、1 停止位、无奇偶校验、LSB first、16 倍过采样。F460
DDL 没有 `USART_HW_FLOWCTRL_NONE` 枚举，代码使用 XHCode/F460 DDL 的 `USART_HW_FLOWCTRL_RTS`
路径实现无 CTSE 的配置；没有接入硬件 RTS/CTS 业务控制。

- PH02：USART3 RX；PC13：USART3 TX。
- DMA1 CH2：USART3 RI，接收端使用自链接 LLP 写入软件 FIFO。
- DMA1 CH3：USART3 TI，发送端使用分段缓冲，启用 TC 中断。
- DMA1 CH3 TC 源映射到 `INT038_IRQn`，优先级 15。
- USART3 TX_EMPTY 保持开启，用于触发发送 DMA。

## 9. 经典 CAN

F460 使用经典 CAN 外设 `CM_CAN`，不是 F448 的 `MCAN1`。当前配置来自 XHCode：

- PB12：CAN TX；PB03：CAN RX。
- 正常工作模式。
- Prescaler=1、TimeSeg1=6、TimeSeg2=2、SJW=2。
- PTB/STB 自动重发，STB 优先级模式关闭。
- 接收溢出保存新帧，接收全部帧关闭。
- 外设状态/接收/发送标志按生成代码配置，但产品 APP 继续轮询接收，不将 CAN 源单独注册到 NVIC。

CAN 位速率和新驱动收发器时钟必须在目标板上用总线分析仪确认，不能只根据寄存器字段推断。

## 10. 关键文件归属

| 文件 | 责任 |
|---|---|
| `.eide/eide.yml` | F460 EIDE target、GCC、包含目录、链接脚本、上传配置 |
| `common/` | F460 设备头文件、系统时钟实现 |
| `driver/` | F460 DDL 及共享 INTC 实现 |
| `GCC/startup_hc32f460.S` | F460 GCC 启动文件和向量表 |
| `Hardware/` | F460 外设初始化和硬件抽象 |
| `User/` | 原 APP 业务、协议、控制 ISR、参数保存 |
| `GCC/hc32f460-app.ld` | APP-A 链接边界、身份段、RAM、ICG 处理 |
| `Core/hc32f460_flash_layout.h` | F460 产品 Flash 地址宏 |
| `Doc/MIGRATION_F460.md` | 本次迁移记录和验证边界 |

## 11. EIDE 构建

在 `MOTOR_APP_F460` 目录执行：

```powershell
$env:DOTNET_ROLL_FORWARD='Major'
& 'C:\Users\13194\.vscode\extensions\cl.eide-3.27.2\res\tools\win32\unify_builder\unify_builder.exe' `
  -p build/F460/builder.params --rebuild --no-color
```

`builder.params` 是 EIDE 生成/缓存的构建参数；日常应以 `.eide/eide.yml` 为工程配置来源。
构建输出位于 `build/F460/`：

- `MOTOR_APP_F460.elf`
- `MOTOR_APP_F460.bin`
- `MOTOR_APP_F460.hex`
- `MOTOR_APP_F460.map`

EIDE 的 Custom 上传路径和 `update.bat` 默认镜像路径均已指向
`build/F460/MOTOR_APP_F460.bin`。

## 12. 尚未完成的验证

以下内容没有目标板、示波器、逻辑分析仪或 CAN/编码器实测证据，不能视为已通过：

- F460 目标板下载、复位和 APP 跳转。
- 200 MHz 时钟、UART 波特率和 CAN 位速率读回。
- TMR4 六路 PWM 波形、死区、异常 Hi-Z 和输出使能。
- ADC1/ADC2 并行触发时序、9 路采样值和 AWD 过流响应。
- KTH7112 SPI Mode 3、CS 时序、DMA 收发和 CRC。
- USART3 DMA 环形接收、分段发送和 Modbus RTU。
- F460 经典 CAN 的收发、仲裁、重发和错误恢复。
- 电机闭环、堵转/过流保护和新驱动板功率级适配。
- F460 专用 Loader 对 APP-A/APP-B、INFO、SAVE 区域和 ICG 的实际兼容性。

因此本工程适合作为 F460 适配基线，正式替换主控或驱动前仍需要先确认 PCB 引脚、电源/晶振、
功率级使能极性、驱动器故障输入以及 F460 专用 BOOT/烧录流程。
