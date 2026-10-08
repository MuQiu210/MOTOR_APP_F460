# MOTOR_APP 与 MOTOR_APP_XHCode 软硬差异复核

## 1. 对比范围与判断规则

本轮对比基于用户修改后的 XML 和重新生成的工程：

- 正式 APP 外设实现：`MOTOR_APP/Hardware/`、`MOTOR_APP/User/`
- XHCode 配置文件：[`App_F448.xml`](App_F448.xml)
- XHCode 当前 EIDE 实际源文件：[`Eclipse/source/main.c`](../../MOTOR_APP_XHCode/Eclipse/source/main.c)
- XHCode EIDE 工程配置：[`Eclipse/.eide/eide.yml`](../../MOTOR_APP_XHCode/Eclipse/.eide/eide.yml)

本文将差异分为两类：

- **硬性差异**：`App_F448.xml` 中已有明确配置项，但值与正式 APP 的外设接口、触发链或时序要求不一致。此类问题应回到 XHCode 图形界面/XML 修正。
- **软性差异**：XML 和生成代码只提供了外设框架或目标数据，地址、缓冲区、阈值、ISR 内容、启动顺序和协议行为仍需要用户接入正式 APP。此类差异不应误判为 XML 配置错误。

## 2. 复核结论

### 2.1 硬性差异：当前未发现新的 XML 外设配置错误

上一轮确认的三组 XML 问题已经在当前 [`App_F448.xml`](App_F448.xml) 中修正：

| 上一轮问题 | 当前 XML 状态 | 当前证据 |
|---|---|---|
| TMR4 六路 OCCR/OCMR 缓冲传送为 `IMMED` | 六路均为 `VALLEY` | [`App_F448.xml:214`](App_F448.xml:214) 至 [`App_F448.xml:219`](App_F448.xml:219) |
| TMR4 六路 PWM 端口输出模式为 `OS` | 六路均为 `NORMAL` | [`App_F448.xml:214`](App_F448.xml:214) 至 [`App_F448.xml:219`](App_F448.xml:219) |
| ADC2/ADC3 EOCA 和对应 IRQ 被打开 | ADC2/ADC3 基础/同步配置为 `NONE`，IRQ 表只保留 ADC1 EOCA | [`App_F448.xml:360`](App_F448.xml:360) 至 [`App_F448.xml:363`](App_F448.xml:363)、[`App_F448.xml:379`](App_F448.xml:381) |

当前 XML 的引脚、ADC 通道、TMR4 触发、USART3、SPI1、CAN1、DMA 通道和 IRQ 映射，与正式 APP 的外设接口基本一致。没有证据表明需要继续修改这些 XML 外设选择。

### 2.2 软性差异：生成结果仍不能直接替代正式 APP

当前最需要处理的是下面两项生成结果残留：

1. **TMR4 六路的 buffer link 传送对象仍为 0，风险最高。** 生成代码虽然已经有 `VALLEY`，但六路 `u16BufLinkTransObject` 仍为 `0`，而正式 APP 同时链接比较值和比较模式缓冲区。见第 3.1 节。
2. **生成代码仍额外打开 ADC2/ADC3 AWD 和 EOCA 中断。** 这与当前 XML 不一致，说明生成模板或生成结果没有完全跟随 XML；它不是当前 XML 的硬性错误，但需要在用户代码接入阶段修正。见第 3.2 节。

除此之外，DMA 地址、LLP 生命周期、AWD 阈值、三个中断服务函数、CAN 启动、PWM 安全门控以及 APP 启动流程仍是用户适配项。

## 3. 软性差异与用户适配项

### 3.1 TMR4 buffer link 传送对象未生成

新生成代码的六个 OC 通道均类似于：

```c
stcTmr4OcInit.u16CompareValueBufCond = TMR4_OC_BUF_COND_VALLEY;
stcTmr4OcInit.u16CompareModeBufCond = TMR4_OC_BUF_COND_VALLEY;
stcTmr4OcInit.u16BufLinkTransObject = 0;
```

证据：[`main.c:568`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:568) 至 [`main.c:690`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:690)。正式 APP 使用：

```c
u16BufLinkTransObject = TMR4_OC_BUF_CMP_VALUE | TMR4_OC_BUF_CMP_MD;
```

证据：[`Hardware/timer4.c:45`](../Hardware/timer4.c:45) 至 [`Hardware/timer4.c:50`](../Hardware/timer4.c:50)。DDL 对该字段的说明是控制比较值和比较模式是否参与 buffer link 传送：[`hc32_ll_tmr4.h:157`](../../MOTOR_APP_XHCode/Eclipse/driver/inc/hc32_ll_tmr4.h:157) 至 [`hc32_ll_tmr4.h:163`](../../MOTOR_APP_XHCode/Eclipse/driver/inc/hc32_ll_tmr4.h:163)。

**判断：软性差异，但属于高风险适配项。** 当前 XML 已经把传送条件改为 Valley，剩余 link 对象是生成代码/用户代码层的缺口，不是 XML 中仍存在的配置错误。接入控制算法前，应让六路通道的 link 对象与正式 APP 一致，并通过寄存器和 PWM 波形确认更新只在谷底生效。

### 3.2 ADC2/ADC3 中断的生成残留

当前 XML 已关闭 ADC2/ADC3 的 EOCA 和 AWD 中断：

- ADC2/ADC3 基础和同步配置：[`App_F448.xml:360`](App_F448.xml:360) 至 [`App_F448.xml:363`](App_F448.xml:363)
- IRQ 表：[`App_F448.xml:379`](App_F448.xml:379) 至 [`App_F448.xml:381`](App_F448.xml:381)

但新生成代码仍执行：

```c
ADC_AWD_IntCmd(CM_ADC2, ADC_AWD_INT_AWD0, ENABLE);
ADC_AWD_IntCmd(CM_ADC3, ADC_AWD_INT_AWD0, ENABLE);
ADC_IntCmd(CM_ADC2, ADC_INT_EOCA, ENABLE);
ADC_IntCmd(CM_ADC3, ADC_INT_EOCA, ENABLE);
```

证据：[`main.c:522`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:522) 至 [`main.c:529`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:529)。生成工程没有 ADC2/ADC3 handler，正式 APP 也只使用 ADC1 作为快速控制入口：[`Hardware/adc.c:76`](../Hardware/adc.c:76) 至 [`Hardware/adc.c:92`](../Hardware/adc.c:92)。

**判断：软性差异/生成结果残留，不是当前 XML 硬性错误。** 用户适配时应删除 ADC2/ADC3 的 `ADC_AWD_IntCmd` 和 `ADC_IntCmd`，保留 ADC2/ADC3 的 AWD 监视与同步采样配置；ADC2/ADC3 的状态由 ADC1 快速 ISR 统一检查和清除。

### 3.3 DMA 地址为占位值，DMA 缓冲区尚未接入

当前 XML 已正确配置通道、触发源、计数、数据宽度和地址递增方向：

| DMA 通道 | 用途 | XML 关键配置 |
|---|---|---|
| DMA1 CH4 | USART3 RX -> 内存 | `USART3_RI`、512 字节、源固定、目的递增、LLP 开启 |
| DMA1 CH5 | 内存 -> USART3 TX | `USART3_TI`、单段重装、源递增、目的固定、TC 中断 |
| DMA2 CH0 | SPI1 RX -> 内存 | `SPI1_SPRI`、4 字节、源固定、目的递增 |
| DMA2 CH1 | 内存 -> SPI1 TX | `SPI1_SPTI`、3 字节、源递增、目的固定 |

XML 证据：[`App_F448.xml:383`](App_F448.xml:383) 至 [`App_F448.xml:391`](App_F448.xml:391)。但是 XML 的 `Sar/Dar` 是 `00000000`，生成代码也保持为 `0x00000000`：[`main.c:255`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:255) 至 [`main.c:347`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:347)。这是 XHCode 不知道用户缓冲区符号和外设寄存器地址的正常占位，不能判定为 DMA 通道配置错误。

用户需要接回正式实现中的地址和生命周期：

- USART3 `RDR/TDR` 与 UART RX 环形缓冲区、TX 分段缓冲区：[`Hardware/uart3.c:89`](../Hardware/uart3.c:89) 至 [`Hardware/uart3.c:143`](../Hardware/uart3.c:143)
- SPI1 `DR` 与编码器 RX/TX 缓冲区：[`Hardware/encoder.c:163`](../Hardware/encoder.c:163) 至 [`Hardware/encoder.c:187`](../Hardware/encoder.c:187)

另外，生成的 `App_DmaCfg()` 将局部变量 `stcLlpDesc` 的地址交给 DMA：[`main.c:251`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:251) 至 [`main.c:286`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:286)。函数返回后该描述符不具备可靠的长期存储期。正式 APP 使用静态 LLP 描述符，因此用户适配时必须把描述符和相关缓冲区改为静态存储，并确认位于 DMA 可访问内存。

### 3.4 ADC AWD 阈值仍是生成模板示例

生成代码使用约 `1.5 V` 和 `2.5 V` 的示例阈值：[`main.c:506`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:506) 至 [`main.c:516`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:516)。正式 APP 当前使用 `LOW=48`、`HIGH=4048`：[`Hardware/adc.c:7`](../Hardware/adc.c:7) 至 [`Hardware/adc.c:10`](../Hardware/adc.c:10)。

该值依赖采样电路、运放偏置、ADC 参考电压和过流保护策略。它不是 XML 引脚或 ADC 通道选择问题，应由用户按正式硬件和保护需求适配，并用实际 ADC 电压确认。

### 3.5 中断服务函数为空

新生成工程的三个 handler 只有 USER CODE 空区：

- ADC1：[`main.c:400`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:400) 至 [`main.c:406`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:406)
- TMR4 谷底：[`main.c:408`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:408) 至 [`main.c:414`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:414)
- USART3 TX DMA：[`main.c:416`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:416) 至 [`main.c:422`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:422)

需要分别接回：

- ADC1 ISR：清状态、检查三组 AWD、读取 ADC/DMA 数据、调用 `InterruptUpdate()`、更新 PWM，并按正式节拍请求 PendSV。正式实现见 [`User/motor.c:49`](../User/motor.c:49) 至 [`User/motor.c:109`](../User/motor.c:109)。
- TMR4 谷底 ISR：清除谷底状态、拉低编码器 CS、清 SPI/DMA 状态并启动 SPI DMA 帧。正式实现见 [`Hardware/encoder.c:109`](../Hardware/encoder.c:109) 至 [`Hardware/encoder.c:121`](../Hardware/encoder.c:121)。
- DMA1 CH5 ISR：从 UART 软件发送 FIFO 装载下一段数据并重启 TX DMA。正式实现见 [`Hardware/uart3.c:289`](../Hardware/uart3.c:289) 至 [`Hardware/uart3.c:317`](../Hardware/uart3.c:317)。

### 3.6 CAN 初始化缺少运行时启动动作

生成代码完成 `MCAN_Init()` 后直接结束：[`main.c:833`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:833) 至 [`main.c:881`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:881)。正式 APP 还需要配置全局过滤策略并调用 `MCAN_Start()`：[`Hardware/can.c:54`](../Hardware/can.c:54) 至 [`Hardware/can.c:67`](../Hardware/can.c:67)。

CAN 的时钟、Classic CAN、标称时间段 `6/2`、SJW `2`、RX FIFO0 32 项、TX FIFO 32 项和 8 字节数据段在 XML 与正式 APP 间匹配：[`App_F448.xml:349`](App_F448.xml:349)。因此缺少 `MCAN_GlobalFilterConfig()` 和 `MCAN_Start()` 是运行时用户接入缺口，不是 XML CAN 参数错误。

### 3.7 启动顺序、PWM 安全门控和应用任务尚未接入

生成的 `main()` 只调用外设配置函数，然后进入空循环：[`main.c:359`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:359) 至 [`main.c:398`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:398)。正式 APP 还需要接回：

- `bsp_init()`、SysTick、PendSV 和任务/主循环；
- 编码器、UART 软件 FIFO、CAN/Modbus 服务；
- 控制模型输入输出和保存区；
- PWM 初始占空比、计数器启动和 MOE/PCTS 安全门控。

正式初始化入口见 [`User/bsp.c:47`](../User/bsp.c:47) 至 [`User/bsp.c:74`](../User/bsp.c:74)，PWM 启动和输出门控见 [`Hardware/timer4.c:164`](../Hardware/timer4.c:164) 至 [`Hardware/timer4.c:171`](../Hardware/timer4.c:171)。生成代码还在外设初始化前先执行 `App_IntCfg()`：[`main.c:371`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:371) 至 [`main.c:386`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:386)；用户接入时应在外设状态、DMA 和初值准备完成后再开放对应中断，避免启动阶段产生伪中断。

## 4. 当前已确认匹配的外设配置

| 外设 | 当前 XML/生成值 | 正式 APP 对照 | 结论 |
|---|---|---|---|
| 芯片与引脚 | `HC32F448JCUI-ZFN48TR`；USART3、ADC、CAN1、TMR4、SPI1 引脚映射一致 | `Hardware/` 各初始化文件 | 匹配 |
| 时钟 | XTAL 8 MHz；PLL `M=1,N=100,P=Q=R=4`；HCLK 200 MHz；总线分频一致 | [`Hardware/clock.c:32`](../Hardware/clock.c:32) 至 [`Hardware/clock.c:78`](../Hardware/clock.c:78) | 匹配 |
| ADC 通道 | ADC1: PA6/PA3/PA1/PA2；ADC2: PA7/PA4/PB1；ADC3: PB0/PB10 | [`Hardware/adc.c:37`](../Hardware/adc.c:37) 至 [`Hardware/adc.c:57`](../Hardware/adc.c:57) | 匹配 |
| ADC 触发 | ADC1 `TMR4_1_SCMP4`；12 bit、右对齐、三 ADC 单次并行同步 | [`Hardware/adc.c:59`](../Hardware/adc.c:59) 至 [`Hardware/adc.c:85`](../Hardware/adc.c:85) | XML 匹配 |
| TMR4 周期/触发 | `CPSR=0x1388=5000`；`SCCR=0x1324=4900`；三角计数、死区 50 | [`Hardware/timer4.h:23`](../Hardware/timer4.h:23) 至 [`Hardware/timer4.c:162`](../Hardware/timer4.c:162) | 匹配 |
| TMR4 六路输出 | `NORMAL`；OCCR/OCMR `VALLEY`；死区模式 `DEAD_TMR` | [`Hardware/timer4.c:45`](../Hardware/timer4.c:45) 至 [`Hardware/timer4.c:151`](../Hardware/timer4.c:151) | XML 匹配，link 对象未生成 |
| USART3 | 921600、8N1、16 倍过采样、LSB、无硬件流控 | [`Hardware/uart3.c:55`](../Hardware/uart3.c:55) 至 [`Hardware/uart3.c:74`](../Hardware/uart3.c:74) | 匹配 |
| SPI1 | 3-wire full duplex、主机、Mode 3、8 bit、MSB、DIV16 | [`Hardware/encoder.c:146`](../Hardware/encoder.c:146) 至 [`Hardware/encoder.c:158`](../Hardware/encoder.c:158) | 匹配 |
| CAN1 | XTAL、Classic、标称 `6/2`、SJW `2`、RX/TX FIFO 32、8 byte | [`Hardware/can.c:18`](../Hardware/can.c:18) 至 [`Hardware/can.c:49`](../Hardware/can.c:49) | 基础配置匹配 |
| DMA | DMA1 CH4/CH5、DMA2 CH0/CH1 的触发源、数量、位宽、递增方向一致 | [`Hardware/uart3.c:89`](../Hardware/uart3.c:89) 至 [`Hardware/uart3.c:143`](../Hardware/uart3.c:143)、[`Hardware/encoder.c:163`](../Hardware/encoder.c:163) 至 [`Hardware/encoder.c:187`](../Hardware/encoder.c:187) | 框架匹配，地址需接入 |
| IRQ | ADC1 `00`；TMR4 `02`；DMA1 CH5 `15` | [`main.c:222`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:222) 至 [`main.c:240`](../../MOTOR_APP_XHCode/Eclipse/source/main.c:240) | 映射和优先级匹配 |

## 5. 非外设 XML 的工程问题

### 5.1 XML 工程路径仍是旧目录

XML 的工程设置为：

```text
ProName="MOTOR_APP_XHCode"
ProLocation="D:\Desktop\HC_Test_Prj"
```

证据：[`App_F448.xml:397`](App_F448.xml:397)。`ProName` 已更新，但 `ProLocation` 仍指向旧工程目录。这属于 XHCode 工程绑定/生成路径问题，不属于外设 XML 配置错误；应在实际 XHCode 中确认打开、保存和重新生成时使用的目录。

### 5.2 EIDE Release 配置仍不可直接用于正式构建

Debug 配置当前包含 Cortex-M4、`HC32F448`、DDL include 路径和 `GCC/HC32F448xC.ld`：[`eide.yml:21`](../../MOTOR_APP_XHCode/Eclipse/.eide/eide.yml:21) 至 [`eide.yml:70`](../../MOTOR_APP_XHCode/Eclipse/.eide/eide.yml:70)。

Release 仍为 Cortex-M3，缺少 `HC32F448` 宏、DDL include 路径和链接脚本：[`eide.yml:82`](../../MOTOR_APP_XHCode/Eclipse/.eide/eide.yml:82) 至 [`eide.yml:129`](../../MOTOR_APP_XHCode/Eclipse/.eide/eide.yml:129)。这属于工程构建配置问题，不能归入 `App_F448.xml` 的外设硬差异。

## 6. 建议处理顺序

1. 保留当前 XML 修正结果，不再把 TMR4 `NORMAL/VALLEY` 和 ADC2/ADC3 `NONE` 改回旧值。
2. 在用户适配代码中补齐六路 TMR4 的 `u16BufLinkTransObject`，优先验证 PWM 比较值在谷底更新。
3. 删除生成代码中 ADC2/ADC3 的 AWD/EOCA 中断使能，保持 ADC1 为唯一快速控制 IRQ。
4. 将 DMA 源/目的地址、静态环形缓冲区和静态 LLP 描述符接回正式 UART/编码器实现。
5. 接回 ADC1、TMR4 谷底、DMA1 CH5 ISR，并在外设状态准备完成后再开放中断。
6. 接回 CAN 全局过滤器、`MCAN_Start()`、PWM 安全门控、控制模型和正式 APP 启动流程。
7. 修正 EIDE Release 的芯片、宏、include、启动文件和链接脚本；确认 XML 旧工程路径不会影响后续重新生成。

## 7. 验证边界

本轮已完成：

- 当前 XML 的结构解析和关键外设字段静态核对；
- 新生成 `Eclipse/source/main.c` 的实际源入口确认；
- 正式 APP 与生成工程的引脚、触发链、DMA 通道、IRQ 和关键时序对照；
- TMR4 十六进制周期/触发值换算。

本轮未完成、不能据此宣称通过的项目：

- XHCode 工程独立干净编译和 Debug/Release 完整编译；
- 目标板寄存器回读；
- PWM/ADC 触发波形、编码器 SPI DMA、UART DMA 和 CAN 总线实测；
- APP 控制模型、OTA/通信任务和电机台架验证。

因此，本轮结论是：**当前 XML 外设硬性差异已修正且未发现新的 XML 硬错误；新工程仍存在必须由用户完成的高风险生成代码适配，不能直接替代正式 `MOTOR_APP`。**
