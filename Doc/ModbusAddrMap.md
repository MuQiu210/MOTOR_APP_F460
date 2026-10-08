# Modbus Address Map

当前地址表以 `User/modbus_slave.c` 为准。

## 编码规则

- `Save` 在代码里是 `g_save.model_save` 的别名。
- 多寄存器字段统一按高字在前、低字在后传输。
- **所有浮点量统一使用 `*1e6` 标度，占用两个 `uint16` 寄存器传输。**
- 读功能码只支持保持寄存器/输入寄存器读；写功能码只支持单寄存器写/多寄存器写。

### 浮点数转换规则

**读取（上位机）：**
```c
int32_t raw = (high << 16) | low;
float value = (float)raw / 1e6f;
```

**写入（下位机）：**
```c
uint32_t raw = (uint32_t)(int32_t)(current_value * 1e6f);
raw = (addr == high_addr) ? (((uint32_t)data << 16U) | (raw & 0xFFFFU))
                          : ((raw & 0xFFFF0000U) | (uint32_t)data);
float new_value = (float)(int32_t)raw / 1e6f;
```

## 地址空间概览

| 地址范围 | 功能区域 | 权限 | 说明 |
| --- | --- | --- | --- |
| `0x0000 - 0x000F` | 系统信息 | R/W | 系统控制、构建时间、固件 MD5、长度 |
| `0x00F0 - 0x00FF` | 固件身份 | R | 电机型号和 APP 发布版本 ASCII |
| `0x0100 - 0x010D` | 运行监控 | R/W | ADC采样值、PWM控制 |
| `0x0200 - 0x0210` | Command | R/W | 电机控制指令 |
| `0x0300 - 0x0324` | Info | R | 电机运行状态输出 |
| `0x0400 - 0x0427` | Save | R/W | 电机配置参数 |
| `0x0500 - 0x053F` | Inside Map | R/W | 内部编码器补偿表（64个点） |
| `0x0700 - 0x073F` | Outside Map | R/W | 外部编码器补偿表（64个点） |

## 0x0000 - 0x000F (SYSTEM - 系统信息)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0000` | R/W | System Control | `uint16` | 读固定返回 `7`；写入 `1` 触发 `NVIC_SystemReset()` |
| `0x0001 - 0x0004` | R | `g_app_build_version` | `uint64` | 共 4 个寄存器，按 `[63:48]` 到 `[15:0]` |
| `0x0005 - 0x000C` | R | Firmware MD5 | `uint8[16]` | 每个寄存器打包 2 个字节 |
| `0x000D - 0x000E` | R | Firmware Length | `uint32` | `0x000D` 高字，`0x000E` 低字 |
| `0x000F` | R | Reserved | `uint16` | 固定返回 `0` |

## 0x00F0 - 0x00FF (FIRMWARE IDENTITY - 固件身份)

字符串字段固定为 16 字节 ASCII，不足部分用 `0x00` 补齐；每个 Modbus 寄存器按高字节在前存放两个字符。

| 地址 | R/W | 字段 | 编码 | 示例 |
| --- | --- | --- | --- | --- |
| `0x00F0 - 0x00F7` | R | Motor Model / Compatibility ID | ASCII，16 字节 | `NGE-4310p` |
| `0x00F8 - 0x00FF` | R | Firmware Version | ASCII，16 字节 | `APP-V2.0.0` |

固件镜像中的同一身份段位于 APP 绝对地址 `0x00011000`，即 `.bin` 偏移 `0x1000`。BOOT 使用型号字段作为 OTA 兼容 ID；MD5 只校验镜像完整性。

## 0x0100 - 0x010D (MONITOR - 运行监控)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0100` | R | `IU_ADC` | `uint16` | U 相电流 ADC |
| `0x0101` | R | `IV_ADC` | `uint16` | V 相电流 ADC |
| `0x0102` | R | `IW_ADC` | `uint16` | W 相电流 ADC |
| `0x0103` | R | `HALL2_ADC` | `uint16` | 霍尔 2 ADC |
| `0x0104` | R | `HALL1_ADC` | `uint16` | 霍尔 1 ADC |
| `0x0105` | R | `PWR_24V_DET` | `uint16` | 24V 检测 |
| `0x0106` | R | `PWR_5V_DET` | `uint16` | 5V 检测 |
| `0x0107` | R | `MOTOR_TEMP` | `uint16` | 电机温度 ADC |
| `0x0108` | R | `MOS_TEMP` | `uint16` | MOS 温度 ADC |
| `0x0109` | R/W | PWM Enable | `uint16` | 读 `0/1`；写 `0` 关闭，非 `0` 打开（需编译时启用） |
| `0x010A` | R/W | `OCCRUL` | `uint16` | PWM U 比较值（需编译时启用） |
| `0x010B` | R/W | `OCCRVL` | `uint16` | PWM V 比较值（需编译时启用） |
| `0x010C` | R/W | `OCCRWL` | `uint16` | PWM W 比较值（需编译时启用） |
| `0x010D` | R | `Model_U.Motor.Angle` | `uint16` | 仅暴露高 16 位 |

## 0x0200 - 0x0210 (COMMAND - 电机控制指令)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0200` | R/W | `Model_U.Command.Mode` | `int16` | 电机模式 |
| `0x0201 - 0x0202` | R/W | `Model_U.Command.Uq` | `float, x1e6` | q轴电压指令，`0x0201` 高字，`0x0202` 低字 |
| `0x0203 - 0x0204` | R/W | `Model_U.Command.Iq` | `float, x1e6` | q轴电流指令，`0x0203` 高字，`0x0204` 低字 |
| `0x0205 - 0x0206` | R/W | `Model_U.Command.Velocity` | `float, x1e6` | 速度指令，`0x0205` 高字，`0x0206` 低字 |
| `0x0207 - 0x020A` | R/W | `Model_U.Command.Position` | `int64` | 位置指令，共 4 个寄存器，按 `[63:48]` 到 `[15:0]` |
| `0x020B - 0x020C` | R/W | `Model_U.Command.Kp` | `float, x1e6` | 比例增益，`0x020B` 高字，`0x020C` 低字 |
| `0x020D - 0x020E` | R/W | `Model_U.Command.Kd` | `float, x1e6` | 微分增益，`0x020D` 高字，`0x020E` 低字 |
| `0x020F - 0x0210` | R/W | `Model_U.Command.ResetFault` | `uint32` | 故障复位，`0x020F` 高字，`0x0210` 低字 |

## 0x0300 - 0x0324 (RUNINFO - 电机运行状态，只读)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0300` | R | `Model_Y.Enable` | `uint16` | 使能状态 `0/1` |
| `0x0301 - 0x0302` | R | `Model_Y.Duty[0]` | `float, x1e6` | A相占空比，`0x0301` 高字，`0x0302` 低字 |
| `0x0303 - 0x0304` | R | `Model_Y.Duty[1]` | `float, x1e6` | B相占空比，`0x0303` 高字，`0x0304` 低字 |
| `0x0305 - 0x0306` | R | `Model_Y.Duty[2]` | `float, x1e6` | C相占空比，`0x0305` 高字，`0x0306` 低字 |
| `0x0307 - 0x0308` | R | `Model_Y.Info.HallAngle` | `uint32` | 霍尔角度，`0x0307` 高字，`0x0308` 低字 |
| `0x0309` | R | `Model_Y.Info.Mode` | `int16` | 当前电机模式 |
| `0x030A - 0x030B` | R | `Model_Y.Info.Fault` | `uint32` | 故障代码，`0x030A` 高字，`0x030B` 低字 |
| `0x030C - 0x030D` | R | `Model_Y.Info.MosTemp` | `float, x1e6` | MOS管温度，`0x030C` 高字，`0x030D` 低字 |
| `0x030E - 0x030F` | R | `Model_Y.Info.MotorTemp` | `float, x1e6` | 电机温度，`0x030E` 高字，`0x030F` 低字 |
| `0x0310 - 0x0311` | R | `Model_Y.Info.Power` | `float, x1e6` | 功率，`0x0310` 高字，`0x0311` 低字 |
| `0x0312 - 0x0313` | R | `Model_Y.Info.Udc` | `float, x1e6` | 母线电压，`0x0312` 高字，`0x0313` 低字 |
| `0x0314 - 0x0315` | R | `Model_Y.Info.Idc` | `float, x1e6` | 母线电流，`0x0314` 高字，`0x0315` 低字 |
| `0x0316 - 0x0317` | R | `Model_Y.Info.Udq[0]` | `float, x1e6` | d轴电压，`0x0316` 高字，`0x0317` 低字 |
| `0x0318 - 0x0319` | R | `Model_Y.Info.Udq[1]` | `float, x1e6` | q轴电压，`0x0318` 高字，`0x0319` 低字 |
| `0x031A - 0x031B` | R | `Model_Y.Info.Idq[0]` | `float, x1e6` | d轴电流，`0x031A` 高字，`0x031B` 低字 |
| `0x031C - 0x031D` | R | `Model_Y.Info.Idq[1]` | `float, x1e6` | q轴电流，`0x031C` 高字，`0x031D` 低字 |
| `0x031E - 0x031F` | R | `Model_Y.Info.Velocity` | `float, x1e6` | 实际速度，`0x031E` 高字，`0x031F` 低字 |
| `0x0320 - 0x0323` | R | `Model_Y.Info.Position` | `int64` | 实际位置，共 4 个寄存器，按 `[63:48]` 到 `[15:0]` |
| `0x0324` | R | `Model_Y.SaveRequest` | `uint16` | 保存请求 `0/1` |

## 0x0400 - 0x0427 (SAVEINFO - 配置参数，可读写)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0400` | R/W | `g_save.can_id` | `uint16` | CAN 从机 ID |
| `0x0401` | R/W | `g_save.master_id` | `uint16` | CAN 主站/反馈目标 ID |
| `0x0402` | R/W | `Save->Magic` | `uint8` | 魔术字，仅低 8 位有效 |
| `0x0403 - 0x0404` | R/W | `Save->ValidBits` | `uint32` | 有效位标志，`0x0403` 高字，`0x0404` 低字 |
| `0x0405 - 0x0406` | R/W | `Save->Rs` | `float, x1e6` | 定子电阻，`0x0405` 高字，`0x0406` 低字 |
| `0x0407 - 0x0408` | R/W | `Save->Ldq` | `float, x1e6` | dq轴电感，`0x0407` 高字，`0x0408` 低字 |
| `0x0409` | R/W | `Save->Pole` | `int16` | 极对数 |
| `0x040A - 0x040B` | R/W | `Save->AngleOffset` | `int32` | 角度偏移，`0x040A` 高字，`0x040B` 低字 |
| `0x040C - 0x040D` | R/W | `Save->HallRangeValue[0..1]` | `uint16[2]` | 霍尔范围值，每个元素占 1 个寄存器 |
| `0x040E - 0x040F` | R/W | `Save->HallMiddleValue[0..1]` | `uint16[2]` | 霍尔中间值，每个元素占 1 个寄存器 |
| `0x0410 - 0x0412` | R/W | `Save->CurrentAdcZeroPoint[0..2]` | `uint16[3]` | 电流ADC零点，每个元素占 1 个寄存器 |
| `0x0413 - 0x0414` | R/W | `Save->PositionZeroPoint` | `int32` | 位置零点，`0x0413` 高字，`0x0414` 低字 |
| `0x0415 - 0x0416` | R/W | `Save->VelocityConfig.Kp` | `float, x1e6` | 速度环Kp，`0x0415` 高字，`0x0416` 低字 |
| `0x0417 - 0x0418` | R/W | `Save->VelocityConfig.Ki` | `float, x1e6` | 速度环Ki，`0x0417` 高字，`0x0418` 低字 |
| `0x0419 - 0x041A` | R/W | `Save->VelocityConfig.Torque` | `float, x1e6` | 转矩限幅，`0x0419` 高字，`0x041A` 低字 |
| `0x041B - 0x041C` | R/W | `Save->VelocityConfig.Acceleration` | `float, x1e6` | 加速度限制，`0x041B` 高字，`0x041C` 低字 |
| `0x041D - 0x041E` | R/W | `Save->PositionConfig.Kp` | `float, x1e6` | 位置环Kp，`0x041D` 高字，`0x041E` 低字 |
| `0x041F - 0x0420` | R/W | `Save->PositionConfig.Ki` | `float, x1e6` | 位置环Ki，`0x041F` 高字，`0x0420` 低字 |
| `0x0421` | R/W | `g_save.model_mode` | `int16` | 重新使能时恢复的模式 |
| `0x0422 - 0x0423` | R/W | `g_save.position_maximum` | `float, x1e6` | 最大位置，`0x0422` 高字，`0x0423` 低字 |
| `0x0424 - 0x0425` | R/W | `g_save.velocity_maximum` | `float, x1e6` | 最大速度，`0x0424` 高字，`0x0425` 低字 |
| `0x0426 - 0x0427` | R/W | `g_save.torque_maximum` | `float, x1e6` | 最大转矩，`0x0426` 高字，`0x0427` 低字 |

## 0x0500 - 0x053F (INENC- 内部编码器补偿表)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0500 - 0x053F` | R/W | `Save->InsideEncoderMap[0..63]` | `int16[64]` | 内部编码器映射表，每个元素占 1 个寄存器 |

**说明：**
- 实际数组大小由代码定义决定
- 当前硬件限制为 64 个点
- 超出范围的地址访问返回 0 或被忽略

## 0x0700 - 0x073F (OUTENC - 外部编码器补偿表)

| 地址 | R/W | 字段 | 编码 | 说明 |
| --- | --- | --- | --- | --- |
| `0x0700 - 0x073F` | R/W | `Save->OutsideEncoderMap[0..63]` | `int16[64]` | 外部编码器映射表，每个元素占 1 个寄存器 |

**说明：**
- 实际数组大小由代码定义决定
- 当前硬件限制为 64 个点
- 超出范围的地址访问返回 0 或被忽略

## 版本更新说明

### V4 (2026) - 地址空间重新规划

**主要变更：**
1. **地址空间重新分配**，按功能区域划分更清晰
2. **函数模块化**，将大型读写函数拆分为按地址范围的独立函数

**地址映射变更：**

| 功能区域 | 旧地址范围 | 新地址范围 | 变化 |
| --- | --- | --- | --- |
| Command | `0x0200 - 0x0210` | `0x0200 - 0x0210` | 无变化 |
| Info | `0x0211 - 0x0235` | `0x0300 - 0x0324` | +0xEF |
| Save | `0x0236 - 0x025D` | `0x0400 - 0x0427` | +0x1CA |
| Inside Map | `0x0300 - 0x04FF` | `0x0500 - 0x06FF` | +0x200 |
| Outside Map | `0x0500 - 0x06FF` | `0x0700 - 0x08FF` | +0x200 |

**代码结构优化：**
- 读函数拆分为 7 个独立函数（按地址范围）
- 写函数拆分为 6 个独立函数（Info 区域只读）
- 使用统一的 `xxFF` 命名规范

**优势：**
- 地址空间更清晰，每个功能区域有独立的完整地址段
- 更容易扩展，每个区域都有充足的预留空间
- 代码结构更清晰，易于维护和调试
- 便于团队协作，不同功能区域互不干扰

**注意事项：**
- 与旧版本固件完全不兼容
- 所有上位机软件需要更新地址映射
- 需要重新配置所有参数

### V3 (2024) - 统一浮点数格式

**主要变更：**
1. **所有浮点数统一使用 `*1e6` 标度**，占用两个连续寄存器
2. **地址重新分配**，从 0x0211 开始的所有地址往后偏移

**具体变更：**
- `Duty[3]`：从 3 个单寄存器 → 6 个寄存器（每个 Duty 占 2 个寄存器）
- `MosTemp`, `MotorTemp`：从 `x10` 单寄存器 → `x1e6` 两个寄存器
- `Power`, `Udc`, `Idc`：从 `x1000` 单寄存器 → `x1e6` 两个寄存器
- `Kp`, `Kd`：从 `x1000` 单寄存器 → `x1e6` 两个寄存器
- 所有 `Save` 浮点参数：从原生 `float32` 位模式 → `x1e6` 标度格式

**优势：**
- 统一的精度和处理方式
- 更高的分辨率（小数点后 6 位）
- 上位机软件更容易处理
- 避免了不同参数使用不同标度的混乱
