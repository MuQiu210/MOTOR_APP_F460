# MOTOR_APP 电机驱动外设配置与联动说明

## 1. 文档范围

本文只说明电机控制实时链路相关的 MCU 外设和内核资源：

- 时钟树：XTAL、HRC、PLLH、总线分频和 PWC。
- TMR4_1：三相六路互补 PWM、硬件死区、ADC 触发事件和谷值中断。
- ADC1/ADC2/ADC3：三相电流、两路霍尔模拟量、电源和温度采样。
- AOS：TMR4 到 ADC、SPI 到 DMA 的硬件事件路由。
- SPI1：KTH7112 磁编码器通信。
- DMA2：编码器 SPI 收发数据转运。
- SysTick、PendSV、NVIC：控制任务和中断调度。

CAN、USART3、Modbus、Flash 参数保存等功能不在本文重点范围内。

本文依据当前工作区源码静态整理，主要源码入口如下：

- 初始化总入口：User/bsp.c:47-73
- 电机快速控制胶水层：User/motor.c:49-130
- ADC：Hardware/adc.c:15-93、Hardware/adc.h:9-18
- PWM 和 TMR4 事件：Hardware/timer4.c:9-171、Hardware/timer4.h:9-31
- 磁编码器 SPI/DMA：Hardware/encoder.c:83-230
- 时钟：Hardware/clock.c:10-79

---

## 2. 总体结构

这个工程不是由 CPU 软件延时逐个控制采样和串行收发，而是让 TMR4、ADC、AOS、SPI 和 DMA 组成一条硬件定时链路：

~~~text
                         +------------------+
                         |     TMR4_1       |
                         |  三相互补 PWM     |
                         +----+--------+----+
                              |        |
             谷值 CNT=0       |        | SCMP4/CNT_UP=4900
                              |        |
                              v        v
                    +---------+--+  +--+---------+
                    | TMR4 谷值中断 |  |     AOS     |
                    | 启动 SPI/DMA |  | 事件路由器   |
                    +---------+----+  +--+---------+
                              |          |
                              v          v
                    +---------+----+  +--+---------+
                    | SPI1 + DMA2 |  | ADC1/2/3  |
                    | KTH7112角度 |  | 同步采样    |
                    +---------+----+  +--+---------+
                              |          |
                              +-----+----+
                                    v
                           +--------+---------+
                           |    ADC1_Handler  |
                           | 过流检查/读角度   |
                           | InterruptUpdate |
                           | 更新 PWM 比较值   |
                           +--------+---------+
                                    |
                                    v
                              下一 PWM 周期
~~~

核心思想是：

1. TMR4 负责产生功率级所需的 PWM，并产生固定位置的 ADC 触发点。
2. ADC1/2/3 在同一硬件触发下完成一次同步采样。
3. TMR4 谷值中断提前启动下一次编码器 SPI 传输。
4. ADC1 中断到来时检查 DMA 是否完成，并把角度和 ADC 数据交给快速控制模型。
5. 快速控制模型计算三相占空比，软件只更新 TMR4 的比较值。
6. 较慢的 MainUpdate() 由 PendSV 执行，系统调度节拍由 SysTick 提供。

---

## 3. 时钟树与时钟分频

### 3.1 配置内容

外部晶振在 Core/system_hc32f448.h:70 定义为：

~~~c
#define XTAL_VALUE (8000000UL)
~~~

Hardware/clock.c 的主要配置为：

| 项目 | 配置 | 作用 |
|---|---:|---|
| HRC | 使能 | 在切换时钟源和配置 PLL 时提供临时安全时钟 |
| XTAL | 使能，振荡器模式，稳定时间 2 ms | 作为 PLL 输入时钟 |
| PLLM | 1 - 1 | 输入分频为 1 |
| PLLN | 100 - 1 | 倍频系数为 100 |
| PLLP | 4 - 1 | PLLP 输出分频为 4 |
| PLLQ | 4 - 1 | PLLQ 输出分频为 4 |
| PLLR | 4 - 1 | PLLR 输出分频为 4 |
| 系统时钟源 | PLLH | 最终系统时钟切换到 PLL |
| Flash 等待周期 | EFM_WAIT_CYCLE3 | 适配高频系统时钟下的 Flash 读取 |
| 高速驱动能力 | PWC_LowSpeedToHighSpeed() | 将芯片从低速能力切换到高速工作能力 |

按 8 MHz 外部晶振和上述 PLL 参数计算：

~~~text
PLL 输入       = 8 MHz / 1       = 8 MHz
PLL VCO        = 8 MHz * 100     = 800 MHz
PLLP/PLLQ/PLLR = 800 MHz / 4     = 200 MHz
~~~

clock_init() 中的总线分频为：

| 时钟 | 分频 | 按 200 MHz 系统时钟计算的名义频率 |
|---|---:|---:|
| HCLK | DIV1 | 200 MHz |
| EXCLK | DIV2 | 100 MHz |
| PCLK0 | DIV1 | 200 MHz |
| PCLK1 | DIV2 | 100 MHz |
| PCLK2 | DIV4 | 50 MHz |
| PCLK3 | DIV4 | 50 MHz |
| PCLK4 | DIV2 | 100 MHz |

### 3.2 运行效果

时钟树决定以下外设的时间基准：

- TMR4 的计数频率和 PWM 载波频率。
- SPI1 的串行位时钟。
- ADC 采样和转换相关的外设时钟。
- SysTick 的 1 ms 节拍。
- DMA、AOS 和其他总线访问的运行速度。

SysTick_Init(1000UL) 最终使用 HCLK_VALUE / 1000 配置 1 kHz 的内核 SysTick，因此系统调度节拍依赖 HCLK 正确切换到高速 PLL。

### 3.3 需要注意的边界

代码明确配置了 TMR4 使用内部时钟和 DIV1，但 TMR4 外设时钟具体来自哪个 PCLK 以及芯片内部的最终门控路径，需要结合 HC32F448 时钟树手册或寄存器读回确认。因此，PWM 的实际频率不能只由 PWM_TIM_CNT=5000 单独确定。

---

## 4. TMR4_1 三相 PWM

### 4.1 基本计数配置

源码：Hardware/timer4.c:31-39。

| 配置项 | 当前值 | 作用 |
|---|---:|---|
| 外设实例 | CM_TMR4_1 | 电机三相 PWM 专用定时器 |
| 计数时钟 | TMR4_CLK_SRC_INTERNCLK | 使用内部外设时钟 |
| 计数分频 | TMR4_CLK_DIV1 | 不对计数时钟继续分频 |
| 计数模式 | TMR4_MD_TRIANGLE | 上下计数，形成中心对齐 PWM |
| 周期值 | PWM_TIM_CNT=5000 | PWM 比较计数的基本尺度 |

三角计数模式下，计数器大致经历：

~~~text
0 -> 5000 -> 0 -> 5000 -> 0 -> ...
~~~

相较于单向锯齿计数，中心对齐 PWM 可以让开关边沿在载波周期中对称分布，有利于三相电流采样和降低部分谐波影响。

### 4.2 六路输出和引脚

| 相别 | 高侧输出 | 低侧输出 | GPIO 引脚 |
|---|---|---|---|
| U 相 | TMR4_1-OUH | TMR4_1-OUL | PA8 / PB13 |
| V 相 | TMR4_1-OVH | TMR4_1-OVL | PA9 / PB14 |
| W 相 | TMR4_1-OWH | TMR4_1-OWL | PA10 / PB15 |

代码通过 GPIO_SetFunc(..., GPIO_FUNC_2) 将这六个引脚切换到 TMR4 复用功能。若只启动 TMR4 而没有配置 GPIO 复用，定时器内部会运行，但功率管引脚不会得到对应波形。

### 4.3 互补输出和硬件死区

工程分别配置了高侧 OCMRH 和低侧 OCMRL 状态表：

- 高侧在相应比较事件时置高或置低。
- 低侧使用相反的比较动作形成互补输出。
- 非匹配的峰值/谷值事件使用 HOLD，避免零占空比时出现额外翻转。
- PWM 模式为 TMR4_PWM_MD_DEAD_TMR。
- U/V/W 三个 PWM 组的 PDAR 和 PDBR 都写入 PWM_DEADTIME=50。

这意味着死区由 TMR4 硬件插入，不是 CPU 延时循环。死区的实际时间还取决于 TMR4 的死区时钟源和分频配置，源码中的数值 50 首先是定时器计数单位。

### 4.4 PWM 比较值和输出门控

Hardware/timer4.h 定义：

~~~c
#define PWM_TIM_CNT  (5000U)
#define PWM_DEADTIME (50U)
#define PWMOUT_MAX   (4330U)
~~~

快速控制中将模型输出转换为比较值：

~~~c
PWMOUT_U = (uint16_t)(Model_Y.Duty[0] * PWMOUT_MAX);
PWMOUT_V = (uint16_t)(Model_Y.Duty[1] * PWMOUT_MAX);
PWMOUT_W = (uint16_t)(Model_Y.Duty[2] * PWMOUT_MAX);
~~~

因此，模型中的 Duty 并不是直接写入定时器计数器，而是先乘以 4330。在当前代码中，软件比较值的上限为 4330/5000=86.6% 的周期计数尺度。

PWM 还有独立的总输出门控：

- PWMOUT_ENABLE() 设置 TMR4 的 MOE 和 PCTS。
- PWMOUT_DISABLE() 清除 MOE 和 PCTS。
- timer4_init() 先写入中间比较值、启动计数器，但保持输出门控关闭。
- motor_init() 完成模型初始化后调用 PWMOUT_ENABLE()。

这样可以先让定时器稳定运行，再打开功率输出，降低上电瞬间误导通的风险。

当 Model_Y.Enable 为假时，当前 User/motor.c 将 U/V/W 三个软件比较值写成 0。代码注释将该路径定义为制动路径；最终高低侧的实际电平仍需结合 TMR4 状态表、栅极驱动器极性和示波器波形确认。

### 4.5 TMR4 特殊事件

源码使用 TMR4 特殊比较事件作为 ADC 触发点：

~~~c
#define PWM_TRIG_VAL (PWM_TIM_CNT - PWM_DEADTIME - 50)
~~~

当前实际值为：

~~~text
PWM_TRIG_VAL = 5000 - 50 - 50 = 4900
~~~

事件配置为：

- 事件通道：TMR4_EVT_CH_XH
- 输出事件：TMR4_EVT_OUTPUT_EVT4
- 匹配条件：TMR4_EVT_MATCH_CNT_UP
- 比较值：4900

因此，ADC 触发发生在 TMR4 上数阶段的 CNT=4900 匹配点，而不是由软件定时器延时产生。

### 4.6 比较值缓冲

输出比较初始化中将比较值和比较模式的缓冲更新条件都设置为 TMR4_OC_BUF_COND_VALLEY。设计意图是让新的比较值在谷值附近统一生效，减少在 PWM 周期中间改变比较值造成的脉冲不对称。

---

## 5. ADC1/ADC2/ADC3 采样系统

### 5.1 ADC 基本配置

源码：Hardware/adc.c:15-32。

三组 ADC 使用相同的基础配置：

| 配置项 | 当前值 |
|---|---|
| 扫描模式 | ADC_MD_SEQA_SINGLESHOT |
| 分辨率 | ADC_RESOLUTION_12BIT |
| 数据对齐 | 右对齐 |
| 触发方式 | ADC1 序列 A 使用内部硬件事件 |
| 同步方式 | ADC1/ADC2/ADC3 单次并行触发 |

每次 TMR4 触发，三组 ADC 各自扫描已经打开的 Sequence A 通道一次，然后等待下一次硬件触发。

### 5.2 三相电流和其他模拟量映射

| ADC | 通道 | 引脚 | 软件宏 | 信号 |
|---|---:|---|---|---|
| ADC1 | CH0 | PA6 | IW_ADC | W 相电流 |
| ADC1 | CH1 | PA3 | HALL2_ADC | 第二路霍尔模拟量 |
| ADC1 | CH2 | PA1 | PWR_24V_DET | 24 V 电源检测 |
| ADC1 | CH3 | PA2 | PWR_5V_DET | 5 V 电源检测 |
| ADC2 | CH0 | PA7 | IV_ADC | V 相电流 |
| ADC2 | CH1 | PA4 | HALL1_ADC | 第一路霍尔模拟量 |
| ADC2 | CH2 | PB1 | MOTOR_TEMP | 电机温度 |
| ADC3 | CH0 | PB0 | IU_ADC | U 相电流 |
| ADC3 | CH1 | PB10 | MOS_TEMP | MOS 温度 |

三相电流并不是集中在一个 ADC 的三个通道中，而是分散在 ADC1/2/3 的 CH0。通过 ADC 同步模式，它们可以由同一个触发事件启动，便于在同一 PWM 时刻得到一组相位相关的电流数据。

### 5.3 TMR4 到 ADC 的触发关系

配置步骤为：

1. ADC1 Sequence A 选择 ADC_HARDTRIG_EVT0。
2. AOS 将 EVT_SRC_TMR4_1_SCMP4 路由到 AOS_ADC1_0。
3. ADC1 的 Sequence A 硬件触发使能。
4. ADC_SyncModeConfig(ADC_SYNC_ADC1_ADC2_ADC3, ADC_SYNC_SINGLE_PARALLEL_TRIG, 1U) 打开三 ADC 单次并行触发。

ADC_SYNC_SINGLE_PARALLEL_TRIG 的效果是：触发条件到来时，ADC1、ADC2、ADC3 同时开始各自的 Sequence A 扫描，每个 ADC 扫描一次。

### 5.4 模拟看门狗和过流保护

ADC1、ADC2、ADC3 的 CH0 都配置了 AWD0：

~~~c
#define ADC_CURRENT_AWD_LOW  (2048 - 2000)  // 48
#define ADC_CURRENT_AWD_HIGH (2048 + 2000)  // 4048
~~~

配置模式为 ADC_AWD_MD_CMP_OUT，即采样值落在 [48, 4048] 之外时置位看门狗状态。

当前只打开 ADC1 的 AWD0 中断，同时打开 ADC1 的 EOCA 中断。ADC1_Handler() 中会主动查询 ADC1、ADC2、ADC3 三个 AWD0 状态，因此 ADC2/ADC3 的过流状态通过 ADC1 快速中断统一处理。

需要特别注意：Hardware/adc.c 中关于 0.45 V 到 2.85 V 的注释，与宏实际产生的 48/4048 计数值并不一致。文档和硬件保护阈值不能直接使用该电压注释，应该以芯片参考电压、采样电阻/运放增益和实际寄存器值重新核算。

### 5.5 ADC 中断中的控制动作

User/motor.c:49-109 中的 ADC1_Handler() 按以下顺序执行：

1. 清除 ADC1 的 EOCA 状态。
2. 检查三组 ADC 的 AWD0 状态。
3. 调用 encoder_recv() 检查编码器 DMA 是否完成并读取角度。
4. 将两路霍尔、三相电流、电源和温度数据写入 Model_U.Motor。
5. 调用 Simulink 生成的 InterruptUpdate() 执行快速控制计算。
6. 将 Model_Y.Duty[0..2] 转成 TMR4 的 U/V/W 比较值。
7. 每累计 11 次 ADC 中断，置位一次 PendSV 请求慢速控制。

该 ISR 使用 __RAM_FUNC，意图是让快速控制代码在 Flash 擦写或其他 Flash 访问受限场景下仍尽可能减少取指影响。但是否完整满足实时性，需要结合链接脚本和 map 文件确认实际代码段位置。

---

## 6. AOS 硬件事件路由

AOS 是芯片内部的事件连接器，不负责保存采样数据，而是把一个外设产生的事件连接到另一个外设的触发输入。

当前电机链路使用的路由如下：

| 事件源 | AOS 目标 | 被触发的动作 |
|---|---|---|
| EVT_SRC_TMR4_1_SCMP4 | AOS_ADC1_0 | 启动 ADC1 Sequence A，并通过 ADC 同步模式启动 ADC2/ADC3 |
| EVT_SRC_SPI1_SPRI | AOS_DMA2_0 | SPI1 接收事件触发 DMA2 CH0 搬运一个字节 |
| EVT_SRC_SPI1_SPTI | AOS_DMA2_1 | SPI1 发送空事件触发 DMA2 CH1 搬运一个字节 |

这种连接的效果是：

- ADC 采样点由 TMR4 硬件比较事件决定，不依赖 CPU 响应延迟。
- SPI 每收发一个字节就能自动触发一次 DMA，CPU 不需要逐字节读写 SPI 数据寄存器。
- TMR4、ADC、SPI、DMA 可以在硬件事件层面联动，减少快速环中的软件抖动。

---

## 7. SPI1 磁编码器接口

### 7.1 引脚和 SPI 配置

源码：Hardware/encoder.c:9-25、Hardware/encoder.c:134-158。

| 项目 | 配置 |
|---|---|
| 传感器 | KTH7112 磁编码器 |
| SPI 实例 | CM_SPI1 |
| 线制 | SPI_3_WIRE |
| 主从 | 主机模式 |
| 传输模式 | 全双工配置 |
| SPI 模式 | Mode 3 |
| 数据宽度 | 8 bit |
| 首位顺序 | MSB first |
| 帧级别 | 1 frame |
| 波特率分频 | SPI_BR_CLK_DIV16 |
| 源码注释频率 | 6.25 MHz |

引脚连接为：

| 功能 | 引脚 |
|---|---|
| MOSI/双向数据路径 | PB3 |
| MISO/输入路径 | PB4 |
| SCK | PB5 |
| 软件片选 CS | PB6 |

PB3/PB4 同时涉及调试复用功能，gpio_init() 中关闭了 SWO/TRST 调试复用，使 SPI1 复用功能能够接管这些引脚。

### 7.2 一帧数据

编码器一帧长度为 4 字节：

~~~text
发送：0x00, 0xFF, 0xFF, 0xFF
接收：命令回显/状态, angle_hi, angle_lo, crc
~~~

源码使用 rx_buf[1] 和 rx_buf[2] 组成 16 位原始角度，再转换成控制模型使用的定点格式：

~~~text
Model_U.Motor.Angle = raw_angle_16 << 16
~~~

encoder_recv() 只在 DMA 接收完成且 CRC-8/ITU 校验通过后更新角度。CRC 失败时保留上一次有效角度，连续失败达到 3 次后返回传感器故障。

### 7.3 SPI 传输的启动和结束

SPI 传输不是由独立 SPI 中断启动，而是由 TMR4 谷值中断启动：

1. TMR4_1_Ovf_Udf_Handler() 检查谷值标志。
2. encoder_start_frame() 拉低 PB6 CS。
3. 清理 SPI 状态和 DMA 完成状态。
4. 打开 DMA2 CH0 接收和 DMA2 CH1 发送。
5. CPU 手动写入第一个命令字节 tx_buf[0]。
6. 后续字节由 SPI 事件触发 DMA 自动收发。
7. ADC1 ISR 调用 encoder_recv()，检查 DMA2 CH0 完成标志。
8. 传输完成后拉高 CS，校验 CRC，并更新角度。

---

## 8. DMA2 编码器数据转运

### 8.1 DMA2 通道配置

源码：Hardware/encoder.c:160-190。

| 通道 | 方向 | 源地址 | 目的地址 | 数量 | 地址变化 | 中断 |
|---|---|---|---|---:|---|---|
| DMA2 CH0 | SPI 接收 | SPI1 DR | rx_buf | 4 字节 | 源固定，目的递增 | 关闭 |
| DMA2 CH1 | SPI 发送 | tx_buf[1..3] | SPI1 DR | 3 字节 | 源递增，目的固定 | 关闭 |

两路 DMA 都使用：

- DMA_DATAWIDTH_8BIT
- DMA_BLOCK_SIZE=1
- 单次 4 字节接收/3 字节发送
- DMA2 全局使能

### 8.2 为什么第一字节由 CPU 发送

编码器传输开始时，SPI 发送空事件尚未形成，因此代码先执行：

~~~c
SPI_WriteData(ENC_SPI, tx_buf[0]);
~~~

该字节启动 SPI 时钟和第一轮收发。之后 SPI 的发送空和接收事件才能分别触发 DMA2 CH1 和 CH0，完成剩余数据搬运。

### 8.3 DMA 完成检测

编码器 DMA 没有单独的 DMA 中断：

- DMA2 CH0 的传输完成标志由 encoder_recv() 轮询。
- encoder_recv() 在 ADC1 快速中断中执行。
- 若当前 ADC1 ISR 到来时 DMA 尚未完成，则本次不更新角度。
- 若连续 3 次无法获得有效数据，则报告 MotorSensorLoss。

这种设计减少了 DMA 中断数量，但它要求 SPI 传输必须在下一次 ADC1 ISR 读取前完成，否则会增加角度数据延迟甚至触发传感器故障。

---

## 9. 内核定时资源和中断优先级

### 9.1 SysTick

初始化代码为：

~~~c
SysTick_Init(1000UL);
~~~

SysTick_Handler() 每 1 ms 调用一次：

~~~c
task_on_timer(&g_task_handler, 1);
~~~

它只负责给工程任务调度器累计节拍。主循环在 User/main.c 中持续调用 task_on_loop()，执行到期的低速任务。

### 9.2 PendSV

ADC1_Handler() 每 11 次快速中断设置一次 PENDSVSET。PendSV 优先级设置为 DDL_IRQ_PRIO_03，处理内容包括：

- 调用生成模型的 MainUpdate()。
- 更新红绿状态灯。
- 处理保存请求。

因此，MainUpdate() 不是每次 ADC 采样都执行，而是由快速环按计数降频触发。它的实际频率取决于 ADC1 EOCA 的实际触发频率。

### 9.3 电机链路中断优先级

当前源码配置为：

| 中断 | 优先级宏 | 主要职责 |
|---|---|---|
| ADC1_IRQn | DDL_IRQ_PRIO_00 | ADC 完成、过流检查、编码器结果读取、快速控制和 PWM 更新 |
| TMR4_1_OVF_UDF_IRQn | DDL_IRQ_PRIO_02 | 在 TMR4 谷值启动编码器 SPI/DMA |
| PendSV_IRQn | DDL_IRQ_PRIO_03 | 执行 MainUpdate() 等低速控制逻辑 |

优先级数值越小，优先级越高。因此快速 ADC 中断高于 TMR4 谷值中断，TMR4 谷值中断又高于 PendSV。

编码器 DMA 通道本身不打开 DMA 中断，所以 DMA2 不会额外打断 ADC 快速控制路径。

---

## 10. 模块联动时序

### 10.1 一个 PWM 周期内的时间顺序

按照当前 TMR4 配置，关键时序可以表示为：

~~~text
TMR4 CNT=0，谷值
  |
  +--> TMR4_1_Ovf_Udf_Handler
        +--> CS 拉低
        +--> DMA2 CH0/CH1 配置并使能
        +--> CPU 发送编码器第 1 字节
        +--> SPI1 开始 KTH7112 传输

TMR4 上数到 CNT=4900
  |
  +--> SCMP4 特殊事件
        +--> AOS_ADC1_0
              +--> ADC1 Sequence A 触发
              +--> ADC2/ADC3 同步并行触发

ADC1/2/3 转换完成
  |
  +--> ADC1 EOCA -> ADC1_Handler
        +--> 检查 ADC1/2/3 模拟看门狗
        +--> encoder_recv() 检查 DMA2 CH0 完成
        +--> 读取霍尔/电流/电压/温度 ADC 数据
        +--> InterruptUpdate()
        +--> Model_Y.Duty[0..2] -> TMR4 U/V/W 比较值
        +--> 每 11 次置位 PendSV

PendSV
  |
  +--> MainUpdate()

每 1 ms SysTick
  |
  +--> task_on_timer()
        +--> 主循环 task_on_loop() 执行到期任务
~~~

### 10.2 为什么编码器在谷值启动、ADC 在另一个比较点触发

编码器 SPI 传输和 ADC 采样都由 TMR4 产生，但使用不同事件：

- 谷值事件适合启动一帧新的编码器通信，使 SPI 传输与 PWM 周期边界对齐。
- SCMP4 的 CNT=4900 上数匹配点用于 ADC 采样，使采样点固定在 PWM 周期内。
- 从谷值到 CNT=4900 之间留出 SPI/DMA 搬运时间。
- ADC1 ISR 到来时，理论上 DMA2 应已完成，encoder_recv() 可以直接取得最新角度。

如果 SPI 时钟、DMA 延迟或中断阻塞导致 DMA 尚未完成，encoder_recv() 本次不会更新角度。持续不能完成有效读取时，控制模型会收到 MotorSensorLoss 故障。

### 10.3 控制数据的方向

~~~text
ADC1/2/3 电流采样  ----+
两路霍尔 ADC       ----+--> Model_U.Motor --> InterruptUpdate()
KTH7112 编码器角度 ----+
电源/温度 ADC      ----+

InterruptUpdate()
  --> Model_Y.Enable
  --> Model_Y.Duty[0..2]
  --> TMR4 OCCRUL/OCCRVL/OCCRWL
  --> 三相功率级 PWM
~~~

其中：

- 三相电流用于电流环和过流保护。
- KTH7112 角度用于 FOC 角度、速度和位置相关计算。
- 两路霍尔模拟量进入模型的霍尔/外部位置处理链路。
- 电源和温度采样用于电压、MOS 和电机温度监控。
- Model_Y.Duty 最终被转换为 TMR4 的硬件比较值。

---

## 11. 初始化顺序对运行效果的影响

bsp_init() 当前顺序为：

~~~text
task_init
clock_init
flash_init
gpio_init
can1_init
timer4_init
adc_init
encoder_init
uart3_init
SysTick_Init
save_init
motor_init
...
~~~

对电机实时链路而言，重要关系是：

1. 时钟先稳定，后续 TMR4、ADC、SPI、SysTick 才有确定的时间基准。
2. TMR4 先配置并启动计数器，但初始输出门控关闭。
3. ADC 配置 TMR4 触发关系和同步模式。
4. 编码器配置 SPI1、DMA2，并打开 TMR4 谷值中断。
5. motor_init() 初始化生成模型并打开 PWM 总输出门控。

如果在 timer4_init()、adc_init()、encoder_init() 或 motor_init() 之间改变顺序，需要重新检查：

- TMR4 事件是否可能在 ADC 配置完成前产生。
- ADC1 中断是否可能早于编码器 DMA 初始化触发。
- PWM 是否可能在模型输出和保护路径准备好之前打开。

---

## 12. 当前源码明确配置与待实测项目

### 12.1 可以直接从源码确认的内容

- 使用 HC32F448 的 TMR4_1 生成 U/V/W 六路互补 PWM。
- PWM 为三角计数模式，周期比较尺度为 5000。
- 硬件死区配置值为 50 个定时器计数单位。
- ADC1/2/3 共打开 9 个模拟采样通道。
- ADC1/2/3 配置为单次并行同步触发。
- TMR4 SCMP4 通过 AOS 触发 ADC。
- TMR4 谷值中断启动 KTH7112 的 SPI1 传输。
- DMA2 CH0/CH1 分别负责 SPI 接收和发送。
- ADC1 ISR 负责快速控制和 PWM 比较值更新。
- SysTick 为 1 kHz，PendSV 承担降频后的 MainUpdate()。

### 12.2 不能只靠静态源码确认的内容

以下内容需要目标板寄存器读回、逻辑分析仪或示波器验证：

- TMR4 实际 PWM 载波频率。
- 六个功率管驱动引脚的实际高低电平极性。
- 50 个计数单位对应的实际死区时间。
- SCMP4 到 ADC 开始转换和 EOCA 中断的真实延迟。
- SPI1 实际 SCK 频率、CS 时序和 KTH7112 波形质量。
- DMA2 是否总能在 ADC1 ISR 读取前完成一帧。
- ADC 模拟看门狗阈值对应的真实相电流范围。
- 关闭输出或 Duty=0 时外部栅极驱动器的真实制动状态。

建议的第一轮联动验证点是：同时测量 TMR4 的一组 PWM、ADC 触发对应的事件信号、编码器 CS/SCK，以及 ADC1 ISR 或 GPIO 调试探针，从而把源码时序和目标板真实时序对应起来。

