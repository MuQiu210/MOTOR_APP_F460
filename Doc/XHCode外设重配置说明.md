# MOTOR_APP_F460 HC32F460JETA 工程配置与 XHCode 重配置说明

> 文档对象：`D:\\Desktop\\Motor\\MOTOR_APP_F460`
>
> 目标芯片：HC32F460JETA（Cortex-M4）
>
> 当前实现：HC32 官方 DDL/LL 驱动 + 手写 BSP + Simulink 生成控制代码 + EIDE/GCC
>
> 迁移目标：使用官方 XHCode 图形化工具重新建立芯片、时钟、引脚、外设、DMA、AOS 和中断配置，再将生成代码接回现有应用。

## 1. 结论先行

这个工程的外设配置不是集中在一个配置文件中，而是分散在以下几层：

| 层级 | 目录或文件 | 作用 | XHCode 迁移处理 |
|---|---|---|---|
| 芯片及 CMSIS | `Core/`、`Drivers/inc/` | 寄存器、启动文件、系统时钟变量、DDL 头文件 | 保留芯片包和启动文件，按 XHCode 工程模板替换或核对 |
| 外设初始化 | `Hardware/*.c` | 时钟、GPIO、CAN、ADC、TMR4、SPI、DMA、USART | 逐项在 XHCode 中重建，生成代码只负责底层初始化 |
| 应用启动 | `User/bsp.c`、`User/main.c` | 初始化顺序、全局对象、任务入口 | 保留；将原来的 `*_init()` 替换为生成初始化函数或保留兼容包装 |
| 快速控制 ISR | `User/motor.c` | ADC1 中断、过流保护、模型调用、PWM 更新、PendSV | 必须保留并重新绑定生成的 IRQ 名称，不能由 XHCode 普通模板覆盖 |
| 控制模型 | `Model/`、`Model/Code/Model_ert_rtw/` | FOC/位置/速度/电流控制和生成 C 代码 | 不属于 XHCode 外设配置，继续使用原模型链路 |
| 协议与升级 | `Components/`、`User/can_slave.c`、`User/modbus_slave.c` | CAN、Modbus、NVStore、OTA、版本信息 | 保留；只迁移其依赖的 CAN、USART、Flash 和中断底层 |
| 构建与下载 | `.eide/eide.yml`、`Components/linker/*.ld`、`update.bat` | 编译源文件、宏、头文件、Flash 地址、下载命令 | XHCode 重新建工程后必须继续保留这些地址和应用接口 |

关键实时关系：

```text
TMR4_1 PWM 计数
  ├─ valley/CNT=0 中断 -> 启动 KTH7112 的 SPI1 + DMA2 一帧传输
  └─ SCMP4 比较事件 -> AOS -> ADC1 Sequence A
                              └─ ADC1/2 单次并行同步采样
                                   └─ ADC1 EOCA 中断
                                        ├─ 过流状态检查
                                        ├─ 读取编码器 DMA 完成结果
                                        ├─ InterruptUpdate()
                                        └─ 更新 U/V/W PWM 比较值
```

如果 XHCode 生成的配置使 ADC 触发点、PWM 周期、DMA 通道、SPI 时钟或中断优先级发生变化，工程可能仍然能够编译，但电机控制时序已经改变。

## 2. 工程文件总览

### 2.1 根目录文件

| 文件 | 说明 |
|---|---|
| `PROJECT.md` | 工程结构、初始化顺序、构建参数概览；部分旧内容编码异常，以源码和 EIDE 配置为准 |
| `.gitmodules` | `Model` 和 `Components` 两个 Git 子模块来源 |
| `.gitignore` | 忽略 Simulink 缓存、生成中间文件和 EIDE 构建输出 |
| `App.code-workspace` | VS Code 工作区入口，不决定 MCU 外设 |
| `update.bat` | 串口/升级工具下载脚本，不能把应用地址改回 `0x00000000` |
| `can_ota.py`、`modbus_ota.py` | 主机侧 OTA 工具，不参与 MCU 外设初始化 |
| `can_adapter.py`、`hcanbus.dll` | 主机侧 CAN 适配层 |
| `firmware_identity.py` | 主机侧固件身份处理 |
| `RTE_Components.h` | 工程/组件兼容头文件 |

### 2.2 Core：芯片基础层

| 文件/类型 | 作用 | 迁移注意 |
|---|---|---|
| `common/hc32f460.h` | F460 芯片寄存器、外设实例和中断号定义 | 必须与 XHCode 使用的 HC32F460JETA 型号一致 |
| `common/system_hc32f460.c` | `SystemInit()`、`SystemCoreClockUpdate()`、VTOR | APP 当前 `VECT_TAB_OFFSET=0x10000` |
| `GCC/startup_hc32f460.S` | GCC 启动文件、向量表、复位入口 | 核对 ADC1、PendSV、TMR4、DMA1 IRQ 名称 |
| `Core/hc32f460_flash_layout.h` | Flash 布局定义 | 与链接脚本和 BOOT/APP 协议一起核对 |
| `Core/arm_*.h`、`cmsis_*.h` | CMSIS/Cortex-M 支持 | 通常直接保留 |

### 2.3 Drivers：官方 DDL/LL 驱动

当前实际使用的官方模块包括：

- ADC：`hc32_ll_adc.*`
- AOS：`hc32_ll_aos.*`
- 时钟/FCG：`hc32_ll_clk.*`、`hc32_ll_fcg.*`
- GPIO：`hc32_ll_gpio.*`
- DMA：`hc32_ll_dma.*`
- MCAN：`hc32_ll_mcan.*`
- SPI：`hc32_ll_spi.*`
- TMR4：`hc32_ll_tmr4.*`
- USART：`hc32_ll_usart.*`
- 中断/工具：`hc32_ll_interrupts.*`、`hc32_ll_utility.*`
- Flash/保护：`hc32_ll_efm.*`、`hc32_ll_pwc.*`

`User/hc32f4xx_conf.h` 打开了 ADC、AOS、CLK、DMA、EFM、FCG、GPIO、INTC、MCAN、PWC、SPI、TMR4、USART 等模块。XHCode 工程生成后不要关闭 `LL_AOS_ENABLE`、`LL_DMA_ENABLE` 或 `LL_INTERRUPTS_ENABLE`。

### 2.4 Hardware：板级外设文件

| 文件 | 负责内容 | XHCode 对应配置 |
|---|---|---|
| `clock.c/.h` | HRC、外部晶振、PLLH、总线分频、Flash 等待周期 | Clock/Clock Tree/PLL |
| `gpio.c/.h` | 晶振脚、LED、调试脚释放、状态输出 | PinMux/GPIO |
| `can.c/.h` | CAN1 引脚、时钟、Classic CAN、位时序、FIFO、过滤 | MCAN/CAN |
| `timer4.c/.h` | TMR4_1 三相互补 PWM、中心对齐、死区、SCMP4 | TMR4/PWM/Event |
| `adc.c/.h` | ADC1/2、通道重映射、同步采样、过流 AWD | ADC/AWD/Trigger |
| `encoder.c/.h` | SPI1、PB6 片选、DMA2 CH0/CH1、AOS、TMR4 中断 | SPI/DMA/AOS/NVIC |
| `uart3.c/.h` | USART3、DMA1 CH4/CH5、RX 环形 DMA、TX 中断 | USART/DMA/AOS/NVIC |
| `flash.c/.h` | Flash 擦写读接口 | EFM/Flash |
| 其他 `*.h` | 对应用层暴露初始化和数据接口 | 建议保留兼容包装 |

## 3. 芯片、编译和启动地址

EIDE 配置位于 `.eide/eide.yml`：

| 参数 | 当前值 |
|---|---|
| CPU | Cortex-M4 |
| 浮点 | single precision、hard ABI |
| C 标准 | C11 |
| 优化 | fast |
| C 宏 | `__MICROLIB` |
| 链接脚本 | `GCC/hc32f460-app.ld` |
| 主要源码目录 | `Core`、`Drivers/src`、`Hardware`、`User`、`Components/*`、`Model/Code/Model_ert_rtw` |
| 下载 bin | `build/F460/MOTOR_APP_F460.bin` |
| 自定义下载 | 调用 `update.bat` |

XHCode 只负责生成初始化代码时，不能替换 FPU、硬浮点 ABI、链接脚本和模型生成代码选项。

## 4. 时钟配置

配置文件：`Hardware/clock.c`、`common/system_hc32f460.c`。

当前时钟初始化顺序：

1. 打开 HRC，暂时切换系统时钟到 HRC。
2. 关闭 PLLH。
3. 配置总线分频：HCLK DIV1、EXCLK DIV2、PCLK0 DIV1、PCLK1 DIV2、PCLK2 DIV4、PCLK3 DIV4、PCLK4 DIV2。
4. Flash 读等待周期为 `EFM_WAIT_CYCLE3`。
5. 打开外部高速晶振：高驱动、OSC 模式、稳定时间 2 ms。
6. PLLH 源为外部晶振，参数 `M=1,N=100,P=4,Q=4,R=4`。
7. GPIO 读等待周期为 `GPIO_RD_WAIT3`。
8. 切到高速驱动，再把系统时钟切到 PLLH。

源码注释中的 CAN 计算使用 8 MHz XTAL；按 PLL 参数推导 PLLH 为 200 MHz，但实际值仍必须按板上晶振和芯片时钟宏确认。XHCode 中应依次配置：芯片型号 -> 外部晶振 -> PLLH -> HCLK/PCLK -> Flash wait cycle -> 外设时钟源。

## 5. PinMux 和 GPIO

### 5.1 晶振、LED 和调试复用

| 引脚 | 配置 | 说明 |
|---|---|---|
| PH0 | 模拟/XTAL-IN | 外部高速晶振输入 |
| PH1 | 模拟/XTAL-OUT | 外部高速晶振输出 |
| PC14 | 推挽数字输出 | 红灯，低电平有效 |
| PC15 | 推挽数字输出 | 绿灯，低电平有效 |
| PB7 | 推挽数字输出 | 初始化拉低 |
| PB3 | SPI1 复用 | 先释放 SWO |
| PB4 | SPI1 复用 | 先释放 TRST |

`gpio_init()` 在 `clock_init()` 前执行，因此 PH0/PH1 必须先设为晶振所需属性。PB3/PB4 的 SWO/TRST 调试复用必须释放，否则 SPI 寄存器正常但引脚可能没有有效波形。

### 5.2 TMR4_1 六路 PWM

| 功能 | 引脚 | 复用 |
|---|---|---:|
| OUH | PA8 | `GPIO_FUNC_2` |
| OVH | PA9 | `GPIO_FUNC_2` |
| OWH | PA10 | `GPIO_FUNC_2` |
| OUL | PB13 | `GPIO_FUNC_2` |
| OVL | PB14 | `GPIO_FUNC_2` |
| OWL | PB15 | `GPIO_FUNC_2` |

### 5.3 CAN1、SPI1、USART3

| 外设 | 信号 | 引脚 | 复用 |
|---|---|---|---:|
| CAN1 | TX | PB12 | `GPIO_FUNC_50` |
| CAN1 | RX | PA11 | `GPIO_FUNC_57` |
| SPI1 | MOSI/三线数据 | PB3 | `GPIO_FUNC_44` |
| SPI1 | MISO/数据接收 | PB4 | `GPIO_FUNC_45` |
| SPI1 | SCK | PB5 | `GPIO_FUNC_47` |
| SPI1 | CS | PB6 | 普通 GPIO，软件控制 |
| USART3 | RX | PH2 | `GPIO_FUNC_33` |
| USART3 | TX | PC13 | `GPIO_FUNC_32` |

### 5.4 ADC 模拟输入

XHCode 中需要同时配置 ADC 通道映射和 GPIO 模拟输入属性：

| 信号 | ADC/通道 | 引脚 | 应用宏 |
|---|---|---|---|
| U 相电流 | ADC3 CH0 | PB0 | `IU_ADC` |
| V 相电流 | ADC2 CH0 | PA7 | `IV_ADC` |
| W 相电流 | ADC1 CH0 | PA6 | `IW_ADC` |
| Hall2 | ADC1 CH1 | PA3 | `HALL2_ADC` |
| Hall1 | ADC2 CH1 | PA4 | `HALL1_ADC` |
| 24 V 检测 | ADC1 CH2 | PA1 | `PWR_24V_DET` |
| 5 V 检测 | ADC1 CH3 | PA2 | `PWR_5V_DET` |
| 电机温度 | ADC2 CH2 | PB1 | `MOTOR_TEMP` |
| MOS 温度 | ADC3 CH1 | PB10 | `MOS_TEMP` |

## 6. TMR4_1 PWM

配置文件：`Hardware/timer4.c`、`Hardware/timer4.h`。

| 项目 | 当前值 |
|---|---|
| 实例 | `TMR4_1` |
| 时钟源/分频 | 内部时钟 / DIV1 |
| 计数模式 | `TMR4_MD_TRIANGLE`，中心对齐 |
| 周期 | `PWM_TIM_CNT=5000` |
| 初始比较值 | `PWM_INITDUTY=0` |
| PWM 模式 | `TMR4_PWM_MD_DEAD_TMR` |
| PWM 分频 | DIV1 |
| 死区 | `PWM_DEADTIME=50`，每相 PDAR/PDBR 都设置 |
| 输出极性 | `TMR4_PWM_OXH_HOLD_OXL_HOLD` |
| 输出上限 | `PWMOUT_MAX=4330` |
| ADC 触发比较值 | `PWM_TRIG_VAL=4900` |
| ADC 触发事件 | TMR4 EVT4、SCMP4、上数匹配 |
| 输出门控 | `PSCR.MOE` + `SCER.PCTS` |

六个通道先配置动作表、比较值、互补输出和死区，再启动计数器，最后执行 `PWMOUT_DISABLE()`。随后 `motor_init()` 调用 `PWMOUT_ENABLE()`。XHCode 默认 PWM 模板通常不能自动复现动作表和安全门控，必须人工核对或保留手写补充代码。

## 7. ADC1/2

配置文件：`Hardware/adc.c`、`Hardware/adc.h`。

| 项目 | 当前值 |
|---|---|
| 实例 | ADC1、ADC2 |
| 扫描模式 | `ADC_MD_SEQA_SINGLESHOT` |
| 分辨率/对齐 | 12 bit / 右对齐 |
| 通道 | ADC1 4 路、ADC2 5 路，共 9 路 |
| 触发 | ADC1 Sequence A，硬件 EVT0 |
| AOS | `AOS_ADC1_0 <- EVT_SRC_TMR4_1_SCMP4` |
| 同步模式 | ADC1/ADC2 单次并行同步，ADC1 为主触发 |
| 中断 | ADC1 EOCA，优先级 `DDL_IRQ_PRIO_00` |

ADC1/ADC2 的过流通道配置 AWD0，模式为窗口外触发：

```text
LOW  = 2048 - 2000 = 48
HIGH = 2048 + 2000 = 4048
```

只有 ADC1 AWD0 开中断，ADC2/ADC3 状态在 ADC1 ISR 中轮询。不要由 XHCode 把三个 AWD 都改成独立高优先级中断。

`User/motor.c:ADC1_Handler()` 的顺序是：清 EOCA -> 检查三 ADC 过流 -> 读取编码器 DMA -> 读取 9 路 ADC -> `InterruptUpdate()` -> 更新 U/V/W PWM -> 每 10 次请求 PendSV。

## 8. SPI1、DMA2 和 KTH7112 编码器

配置文件：`Hardware/encoder.c`、`Hardware/encoder.h`。

### 8.1 SPI1

| 项目 | 当前值 |
|---|---|
| 实例 | SPI1 |
| 线制 | `SPI_3_WIRE` |
| 主从 | 主机 |
| 模式 | Mode 3 |
| 数据 | 8 bit、MSB first、1 frame |
| 分频 | `SPI_BR_CLK_DIV16` |
| 源码注释频率 | 6.25 MHz，需按实际 SPI 输入时钟确认 |
| CS | PB6 软件 GPIO，空闲高、传输低 |

一帧为 `TX: 00 FF FF FF`；RX 字节 1/2 是 16 位角度，字节 3 是 CRC。CRC-8/ITU 参数为多项式 `0x07`、初值 `0x00`、异或输出 `0x55`。

### 8.2 DMA2 和 AOS

| 通道 | 方向 | 源 | 目的 | 数量 | 中断 |
|---|---|---|---|---:|---|
| DMA2 CH0 | SPI RX | SPI1 DR | `rx_buf` | 4 字节 | 关闭 |
| DMA2 CH1 | SPI TX | `tx_buf[1..3]` | SPI1 DR | 3 字节 | 关闭 |

事件连接：

```text
AOS_DMA2_0 <- EVT_SRC_SPI1_SPRI
AOS_DMA2_1 <- EVT_SRC_SPI1_SPTI
```

传输没有 DMA 中断。TMR4 谷值中断拉低 CS、清 DMA 状态并发送第一个字节；后续字节由 SPI 事件触发 DMA。ADC1 ISR 调用 `encoder_recv()` 轮询 CH0 完成标志，完成后释放 CS、检查 CRC 并更新模型角度。

### 8.3 NVIC

| IRQ | 优先级 | 作用 |
|---|---:|---|
| `TMR4_1_OVF_UDF_IRQn` | `DDL_IRQ_PRIO_02` | 谷值时启动一帧编码器传输 |
| `ADC1_IRQn` | `DDL_IRQ_PRIO_00` | 采样、过流、编码器结果、快速控制、PWM |

## 9. USART3、DMA1 和 Modbus

配置文件：`Hardware/uart3.c`、`Hardware/uart3.h`、`User/modbus_slave.c`、`Components/modbus/`。

### 9.1 USART3

`bsp_init()` 调用 `uart3_init(921600UL)`。参数为：内部时钟 DIV1、921600 bps、8-N-1、16 倍过采样、LSB、无硬件流控、TX/RX 开启。RX 普通中断关闭；TX empty 开启作为 TX DMA 触发。

### 9.2 DMA1

| 通道 | 方向 | 配置 | AOS 事件 |
|---|---|---|---|
| DMA1 CH4 | USART3 RX -> 内存 | 8 bit、源固定、目的递增、512 字节、自链接 LLP、无 DMA 中断 | `AOS_DMA1_4 <- EVT_SRC_USART3_RI` |
| DMA1 CH5 | 内存 -> USART3 TX | 8 bit、源递增、目的固定、分段发送、TC 中断 | `AOS_DMA1_5 <- EVT_SRC_USART3_TI` |

RX/TX 软件 FIFO 均为 512 字节，TX DMA 暂存区为 256 字节。CH5 完成中断 `DMA1_TC5_BTC5_Handler()` 优先级为 `DDL_IRQ_PRIO_15`。如果 XHCode 不支持自链接 LLP，应保留 `uart3.c` 中的描述符初始化。

## 10. 经典 CAN

配置文件：`Hardware/can.c`、`Hardware/can.h`、`User/can_slave.c`、`Components/damiao/`。

| 项目 | 当前值 |
|---|---|
| 实例/时钟 | `CM_CAN` / XTAL |
| 模式/帧 | Normal / Classic CAN |
| 自动重发 | 开启 |
| TX pause | 关闭 |
| 协议异常 | 开启 |
| 标准/扩展过滤器 | 0/0 |
| RX FIFO0 | 32 个元素，每个 8 字节 |
| RX FIFO1、RX buffer、TX event | 0 |
| TX FIFO/Queue | 32 个元素，每个 8 字节 |
| TX 模式 | FIFO |

名义位时序源码值：预分频 1、Seg1 6、Seg2 2、SJW 2，按 8 MHz CAN 时钟计算目标 1 Mbps、采样点约 75%。初始化后又手动把全局过滤设置为非匹配标准/扩展帧接收至 FIFO0；XHCode 生成后必须核对没有恢复成全部拒收。

DaMiao 命令、反馈、广播寄存器和进入 BOOT 的复位路径位于 `User/can_slave.c` 与 `Components/damiao/`，不是普通 CAN 外设配置。

## 11. SysTick、PendSV 和初始化顺序

### 11.1 SysTick

`SysTick_Init(1000UL)` 为 1 kHz。处理函数只调用：

```c
task_on_timer(&g_task_handler, 1);
```

主循环在 `User/main.c` 中持续调用 `task_on_loop(&g_task_handler)`。

### 11.2 PendSV

ADC1 ISR 每 10 次请求一次 PendSV。PendSV 优先级为 `DDL_IRQ_PRIO_03`，执行 `MainUpdate()`、状态灯更新、模型复位故障清除和保存请求处理。不要把 `MainUpdate()` 移入 ADC1 ISR，除非重新评估快速环执行时间。

### 11.3 `bsp_init()` 当前顺序

```text
LL_PERIPH_WE(ALL)
task_init
clock_init
flash_init
gpio_init
can1_init
timer4_init
adc_init
encoder_init
uart3_init(921600)
SysTick_Init(1000)
save_init
motor_init
can_slave_init
modbus_slave_init
save_cycle_register
LL_PERIPH_WP(ALL)
critical_exit(0)
```

PH0/PH1 必须在 `clock_init()` 前配置；PWM 输出门控必须在 TMR4、ADC、SPI/DMA 和模型准备好之前保持安全状态。

## 12. 应用、组件和模型文件

### 12.1 User

| 文件 | 作用 |
|---|---|
| `main.c/.h` | 应用入口和 HC32 LL 总头文件 |
| `bsp.c/.h` | 全局对象、固件身份、初始化顺序 |
| `motor.c/.h` | ADC1/PendSV ISR、模型调用、PWM 更新、状态灯 |
| `save.c/.h` | 参数保存、NVStore 注册、模型保存数据 |
| `can_slave.c/.h` | DaMiao CAN 从机适配、使能、清故障、设零、进 BOOT |
| `modbus_slave.c/.h` | Modbus RTU 从机和寄存器适配 |
| `firmware_storage.c` | 固件信息/存储逻辑 |
| `hc32f4xx_conf.h` | DDL 模块开关 |

### 12.2 Components

| 目录 | 作用 |
|---|---|
| `base` | 基础类型、位域、通用宏 |
| `critical` | 临界区和中断保护 |
| `task` | 协作式任务调度，依赖 SysTick |
| `ringbuffer` | UART 软件缓冲 |
| `nvstore` | 参数持久化，依赖 Flash |
| `md5` | 固件摘要 |
| `modbus` | Modbus RTU 协议栈 |
| `damiao` | DaMiao 电机 CAN 协议 |
| `ota` | OTA 辅助逻辑 |
| `canopen`、`j1939` | 协议组件，是否启用以实际调用和链接结果为准 |
| `syscalls` | newlib syscall 适配 |
| `linker` | APP/Loader 链接脚本 |
| `pack` | CMSIS Pack 文件 |

### 12.3 Model

- `Model/Model.slx`：正式控制模型。
- `Model/Simulation.slx`：仿真模型。
- `Model/Model.sldd`：数据字典。
- `Model/Code/Model_ert_rtw/`：EIDE 实际编译的生成 C 代码。
- `Model/Library/`：硬件、算法、协议共享库。
- `Model/buildfile.m`：模型代码生成脚本。

XHCode 不会替代 Simulink 代码生成。迁移后应保证生成代码中的 `Model_U`、`Model_Y`、`InterruptUpdate()`、`MainUpdate()` 接口仍与 `User/motor.c` 一致。

## 13. Flash、链接脚本和下载

### 13.1 APP

`GCC/hc32f460-app.ld`：

| 区域 | 起始地址 | 大小 |
|---|---:|---:|
| APP FLASH | `0x00010000` | `0x30000` |
| RAM | `0x1FFF8000` | `0x2F000` |
| `.app_version` | `0x00011000` | 48 字节 |
| 栈保留 | RAM 末端 | `0x800` |

`g_app_identity` 放在 `.app_version`，包含 `FID1`、产品号和版本号。XHCode 新建工程时必须保持 APP 起始地址、版本段地址、RAM 地址、栈大小和段名。

### 13.2 Loader

`MOTOR_BOOT_F460/GCC/hc32f460-loader.ld` 的 Loader Flash 从 `0x00000000` 开始，长度 `0x10000`，并在 `0xF000` 放置 `.boot_identity`。它属于 BOOT 工程边界，不应被 APP 配置覆盖。

## 14. XHCode 建工程步骤

1. 选择 HC32F460JETA 的实际料号和封装，并使用匹配的 HC32F460 Pack。
2. 选择与当前工程兼容的编译器和 DDL 版本。
3. 设置外部晶振、PLLH、总线分频和 Flash wait cycle。
4. 按第 5 节配置所有 PWM、CAN、SPI、CS、USART、ADC、LED 和晶振引脚。
5. 配置 TMR4_1 中心对齐 PWM、六路输出、死区和 SCMP4。
6. 配置 AOS：`TMR4_1_SCMP4 -> ADC1_0`。
7. 配置 ADC1/2 的九路通道、ADC1 主触发和 ADC1/2 并行同步。
8. 配置 SPI1 Mode 3、8 bit、MSB、三线协议。
9. 配置 DMA2 CH0/CH1 及 SPI RX/TX AOS 事件。
10. 配置 USART3 921600-8-N-1、DMA1 CH4/CH5 和 USART AOS 事件。
11. 配置经典 CAN 外设、位时序、RX/TX FIFO 和全局过滤。
12. 配置 NVIC：ADC1=00、TMR4=02、PendSV=03、DMA1 TX=15；SysTick=1 kHz。
13. 将生成初始化代码放到独立目录，用 `Hardware/*.c` 做薄包装，避免覆盖业务 ISR。
14. 重新接入 `bsp_init()`，保留当前初始化顺序和 PWM 安全门控。
15. 编译后检查链接地址、向量表、版本段和所有 IRQ 是否重复定义。

## 15. 迁移检查清单

### 必须一致

- [ ] HC32F460JETA 料号和封装。
- [ ] 外部晶振、PLLH M/N/P/Q/R。
- [ ] HCLK/PCLK 分频和 Flash wait cycle。
- [ ] APP 向量表偏移 `0x10000`。
- [ ] APP Flash、RAM、`.app_version` 地址。
- [ ] 六路 TMR4 PWM 引脚和复用号。
- [ ] PWM 三角计数、周期 5000、死区 50、SCMP4=4900。
- [ ] ADC1/2 九路通道映射。
- [ ] ADC1 主触发、ADC1/2 并行同步、EOCA。
- [ ] AWD0 窗口 48~4048，只有 ADC1 AWD0 进中断。
- [ ] SPI1 Mode 3、8 bit、MSB、DIV16。
- [ ] DMA2 CH0 RX、CH1 TX、SPI AOS 事件。
- [ ] USART3 921600-8-N-1。
- [ ] DMA1 CH4 RX LLP、CH5 TX TC 中断、USART AOS 事件。
- [ ] 经典 CAN、FIFO 数量、位时序和全局过滤。
- [ ] ADC1/TMR4/PendSV/DMA1 中断优先级。
- [ ] `bsp_init()` 初始化顺序和 PWM 安全门控。

### 不应直接被 XHCode 覆盖

- [ ] `User/motor.c` 中的 ADC1/PendSV ISR。
- [ ] `Hardware/encoder.c` 中的 KTH7112 帧、DMA 轮询和 CRC。
- [ ] `User/bsp.c` 中的固件身份和初始化顺序。
- [ ] `Components/linker/*.ld` 中的 BOOT/APP 地址。
- [ ] `Model/Code/Model_ert_rtw/` 中的生成模型代码。
- [ ] CAN/Modbus/OTA/NVStore 的协议和参数业务。

## 16. 验证边界

源码可以确认：

- 使用 HC32F460JETA Cortex-M4 和官方 DDL/LL 驱动。
- 使用 TMR4_1、ADC1/2、SPI1、DMA1/2、AOS、USART3、经典 CAN。
- ADC 触发、编码器 DMA 和 PWM 更新存在明确的硬件事件链。
- APP 从 `0x00010000` 链接，版本段位于 `0x00011000`。
- EIDE 是当前工程构建配置入口。

源码不能单独证明：

- 实际 PWM 载波频率和高低桥物理极性。
- ADC 采样延迟以及 DMA 是否总在 ADC1 ISR 前完成。
- 目标板 CAN 物理速率、终端电阻和总线稳定性。
- SPI1 实际 SCK 频率和 KTH7112 波形质量。
- XHCode 生成代码与当前 Pack 默认值完全相同。

迁移后应分别记录“源码一致”“能够编译”“目标板寄存器正确”“电机运行正确”，不能用其中一个结论替代其他结论。
