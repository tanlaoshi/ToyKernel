# 开课 ABI 冻结表（印刷 · 大纲夹页）

> **开课冻结日认本表**。数值以 `User/include/` 宏为准；破坏性改名须升 MAJOR，并改本表 + [`学生速查卡.md`](学生速查卡.md) + [`开课前接口冻结与教学准备.md`](../待做/开课前接口冻结与教学准备.md)。  
> SDK 包号另见 `Tools/Sdk/VERSION` = **`1.0.0-course`**（与下表 CRT/lib 号无关）。

| 组件 | 宏前缀 | 冻结版本 |
| ---- | ------ | -------- |
| CRT / libtoyos | `TOYOS_CRT_VERSION_*` | **1.4.0** |
| libToyGfx | `TOY_GFX_ABI_VERSION_*` | **1.3.0** |
| libToyUi | `TOY_UI_ABI_VERSION_*` | **1.2.0** |
| libToyNet | `TOY_NET_ABI_VERSION_*` | **2.0.1** |
| libFsUtil | `FS_UTIL_ABI_VERSION_*` | **1.0.0** |

**课上承诺**：只升 MINOR / PATCH；不升破坏性 MAJOR（除非发新冻结修订并改讲义）。

**核对（开课前再跑一遍）**：

```bash
rg -n 'VERSION_STRING' User/include/toyos/version.h User/include/ToyGfx.h \
  User/include/ToyUi.h User/include/ToyNet.h User/include/FsUtil.h
```
