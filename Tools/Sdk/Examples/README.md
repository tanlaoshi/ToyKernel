# ToyOS SDK 示例

在**已解压的 SDK 根**下编译（`Library/` 与 `ToySdk.mk` 在上两级）。  
不要在仓库的 `Tools/Sdk/Examples/` 下直接 `make`。

```bash
# 已有 ToySdk/ 时（任意路径）
make -C /path/to/ToySdk/Examples/Hello
# → Examples/Hello/Build/MYAPP.ELF
```

| 目录 | 产物（FAT 8.3） | 库 | Guest |
|------|-----------------|----|--------|
| `Hello/` | `MYAPP.ELF` | CRT | `exec MYAPP.ELF` |
| `File/` | `FILEIO.ELF` | CRT | 写读 `TOYOS:NOTE.TXT`；`lseek` / `fopen` |
| `Dir/` | `DIR.ELF` | CRT | 列默认卷根 |
| `Pipe/` | `PIPE.ELF` | CRT | 子写父读 |
| `Fork/` | `FORK.ELF` | CRT | `fork` + `wait(0)` |
| `Gui/` | `MYGUI.ELF` | Ui + Gfx | 标签/按钮 + 复选框/列表/输入框 |
| `Blit/` | `BLIT.ELF` | Ui + Gfx | `ToyGfxDamageRect` + 点/线/矩形 |
| `Net/` | `MYNET.ELF` | ToyNet | `ToySockAddrIn` + `ToyNetResolve`；先 `lwip on`；`nc -l -p 8888` |
| `Fs/` | `FSUTIL.ELF` | FsUtil | `FsUtilJoin` / `ListDir` / `TOYOS:` |

`make deploy` 复制到 ToyImage 的 `rootfs/`。SDK 不在仓库 `Dist/ToySdk` 时请设 `TOYIMAGE=`。课堂树内模板仍是 `User/Pkg/`。
