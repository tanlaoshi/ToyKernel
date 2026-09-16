# ToyOS SDK 示例（PR-A-examples）

在打包后的 SDK 里编译（不要在 `Tools/Sdk/Examples` 下直接 `make`，那里没有 `Library/`）：

```bash
cd ToyKernel && ./Tools/build-sdk.sh
for d in Dist/ToySdk/Examples/*/; do make -C "$d"; done
```

| 目录 | 产物（FAT 8.3） | 库 | Guest |
|------|-----------------|----|--------|
| `Hello/` | `MYAPP.ELF` | CRT | `exec MYAPP.ELF` |
| `File/` | `FILEIO.ELF` | CRT | 写读 `TOYOS:NOTE.TXT` |
| `Dir/` | `DIR.ELF` | CRT | 列默认卷根 |
| `Pipe/` | `PIPE.ELF` | CRT | 子写父读 |
| `Fork/` | `FORK.ELF` | CRT | `fork` + `wait(0)` |
| `Gui/` | `MYGUI.ELF` | Ui + Gfx | 标签/按钮 + 复选框/列表/输入框 |
| `Blit/` | `BLIT.ELF` | Ui + Gfx | `ToyGfxDamageRect` + 点/线/矩形 |
| `Net/` | `MYNET.ELF` | ToyNet | 先 `lwip on`；宿主机 `nc -l -p 8888` |

`make deploy` 复制到兄弟仓 `ToyImage/rootfs/`（可设 `TOYIMAGE=`）。课堂树内模板仍是 `User/Pkg/`。
