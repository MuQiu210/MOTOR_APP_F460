# stdatomic 兼容层

该目录下的 `stdatomic.h` 提供统一原子接口：

- 当编译标准是 `C11/C17` 且工具链支持原子时：转发到工具链原生 `<stdatomic.h>`。
- 当编译标准是 `C99`（或 `C11` 禁用原子）时：启用基于 GCC `__atomic` 内建指令的兼容实现。

## 使用方式

业务代码直接包含：

```c
#include <stdatomic.h>
```

无需区分 `C99` 或 `C11`。

## 说明

- 该兼容层以 MCU 工程常用接口为目标，覆盖了 `atomic_load/store/exchange`、`compare_exchange`、
  `fetch_add/sub/and/or/xor`、`atomic_flag` 和 `fence`。
- 兼容实现依赖 GCC/Clang 风格的 `__atomic` builtins。
