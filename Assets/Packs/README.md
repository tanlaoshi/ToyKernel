# Assets/Packs — 已安装资源包（PR-S3）

`store install` 对 catalog `type=asset` 的载荷落在此目录（如 `demopack` → `INFO.TXT`）。  
由**已链进内核**的代码按路径读取；**不是** `.ko` 热加载。

空目录时 `store install` 会 `EnsurePacksDir` 创建。
