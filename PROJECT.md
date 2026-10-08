# Motor 项目结构

## 概述

HC32F460 Cortex-M4 电机控制固件，使用 Simulink 代码生成 + 手写 BSP/驱动/组件，EIDE + GCC 工具链构建。

当前 EIDE 构建以 `.eide/eide.yml` 为准，实际使用 `common/`、`driver/`、`GCC/`、
`Hardware/`、`User/`、`Components/` 和 `Model/Code/Model_ert_rtw/`；`Core/` 仅作为
包含目录提供 `hc32f460_flash_layout.h`。目录中保留的旧 F448 芯片头文件、启动文件和
`Drivers/` 驱动源不在 F460 目标的源文件列表中，不参与当前镜像构建。

## 目录结构

```
Motor/
├── App.code-workspace          VSCode 工作区配置
├── update.bat                  Modbus OTA 烧录脚本
├── modbus_ota.py               Modbus OTA 下位机烧录工具（Python）
├── requirements.txt            Python 依赖（pymodbus, pyserial）
│
├── common/                     HC32F460 芯片头文件和系统初始化
│   ├── hc32f460.h              HC32F460 寄存器定义
│   ├── hc32f4xx.h              兼容包含头文件
│   └── system_hc32f460.c/h     系统初始化
│
├── Core/hc32f460_flash_layout.h F460 产品 Flash 布局头文件（仅作包含目录）
│
├── driver/                     HC32F460 DDL 驱动库（XHCode 配置对应版本）
│   ├── inc/                    驱动头文件
│   ├── src/                    驱动实现
│   └── hc32f460_ll_interrupts_share.c 共享中断实现
│
├── GCC/                        GCC 启动文件
│   └── startup_hc32f460.S      汇编启动文件
│
├── Hardware/                   硬件抽象层（板级驱动）
│   ├── adc.c/h                 ADC 多通道采样
│   ├── can.c/h                 经典 CAN 驱动
│   ├── clock.c/h               系统时钟配置
│   ├── encoder.c/h             KTH7112 磁编码器（SPI + DMA）
│   ├── flash.c/h               Flash 擦写读接口
│   ├── gpio.c/h                GPIO 初始化和状态 LED
│   ├── timer4.c/h              TMR4 PWM 输出（三相逆变）
│   └── uart3.c/h               UART3 非阻塞驱动（DMA + ringbuffer）
│
├── User/                       应用层
│   ├── main.c/h                入口点
│   ├── bsp.c/h                 板级支持包（初始化顺序、全局对象）
│   ├── save.c/h                NVStore 持久化和固件元数据
│   ├── motor.c/h               电机 ISR（ADC1 过流保护、PendSV 主循环）
│   ├── can_slave.c/h           DaMiao CAN 从机适配
│   └── modbus_slave.c/h        Modbus RTU 从机（寄存器映射）
│
├── Model/                      Simulink 模型和代码生成
│   ├── Motor.slx               主电机控制模型
│   ├── Simulation.slx          仿真模型
│   ├── Motor.sldd              数据字典
│   ├── Motor.prj               MATLAB 工程
│   ├── buildfile.m             构建脚本
│   ├── Code/                   生成代码输出
│   │   └── Motor_ert_rtw/      Motor 模型生成的 C 代码
│   ├── Cache/                  Simulink 缓存
│   └── Library/                共享库子模块（git submodule）
│
├── Components/                 可复用组件库（git submodule）
│   ├── AGENTS.md               组件开发规范
│   ├── base/                   字节序、位域宏、构建版本
│   ├── critical/               ARM PRIMASK 临界区
│   ├── task/                   协作式任务调度器
│   ├── nvstore/                双 bank NVStore
│   ├── ringbuffer/             无锁环形缓冲区
│   ├── md5/                    MD5 摘要
│   ├── modbus/                 Modbus 协议栈（RTU master/slave）
│   ├── damiao/                 DaMiao CAN 电机协议
│   ├── ota/                    OTA 固件升级辅助
│   ├── j1939/                  J1939-21 传输协议
│   ├── canopen/                CANopen 协议（WIP）
│   ├── dji/                    DJI 串口协议
│   ├── linker/                 GCC 链接脚本
│   ├── pack/                   厂商 CMSIS Pack 文件
│   └── syscalls/               newlib syscall 桩
│
├── Doc/                        项目文档
│   ├── ModbusAddrMap.md        Modbus 寄存器地址映射
│   └── *.pdf                   电机手册
│
├── host/                       上位机配置文件（.mbp）
│
├── .eide/                      EIDE 工程配置
│   ├── eide.yml                构建目标配置
│   └── *.pyocd.yaml            调试器配置
│
├── .vscode/                    VSCode 配置
│   └── tasks.json              构建/烧录任务
│
└── build/                      构建输出（gitignore）
    └── App/                    固件二进制
```

## 模块依赖关系

```
main.c
  └── bsp.c
        ├── save.c              NVStore 持久化
        ├── motor.c             电机控制 ISR
        │     └── save.c        save_request()
        ├── can_slave.c         CAN 从机
        │     └── save.c        save_request()
        └── modbus_slave.c      Modbus 从机
              └── save.c        firmware_info_recv()
```

## 初始化顺序（bsp_init）

```
1. task_init                    调度器初始化
2. clock_init                   系统时钟
3. flash_init                   Flash 驱动
4. gpio_init                    GPIO
5. can1_init                    CAN1
6. timer4_init                  TMR4 PWM
7. adc_init                     ADC 多通道
8. encoder_init                 磁编码器 SPI+DMA
9. uart3_init                   UART3 DMA
10. SysTick_Init                1kHz 系统节拍
11. save_init                   NVStore + 固件元数据
12. motor_init                  电机模型初始化
13. can_slave_init              CAN 从机 + DaMiao 协议
14. modbus_slave_init           Modbus 从机
15. save_cycle_register         注册 NVStore 写入任务
16. LL_PERIPH_WP                恢复外设写保护
17. critical_exit(0)            使能全局中断
```

## 中断优先级

| 中断 | 优先级 | 频率 | 功能 |
|------|--------|------|------|
| ADC1 | 00（最高） | 20 kHz | 过流保护 + 电机控制步进 |
| TMR4_1 OVF/UDF | 02 | 20 kHz | 编码器 SPI 触发 |
| PendSV | 03 | ~1.8 kHz | 主循环（MainUpdate） |
| SysTick | 默认（最低） | 1 kHz | 调度器节拍 |
| UART3 DMA | — | — | Modbus 收发 |

## 构建配置

- **工具链**: GCC ARM (`arm-none-eabi-gcc`)
- **C 标准**: C11 (`-std=c11`)
- **优化**: `-Ofast`
- **浮点**: 硬件 FPU（single precision, hard ABI）
- **链接脚本**: `GCC/hc32f460-app.ld`
- **Flash 起始**: 0x00010000（F460JETA 的 loader 占前 64 KiB）
- **RAM**: 188KB @ 0x1FFF8000

