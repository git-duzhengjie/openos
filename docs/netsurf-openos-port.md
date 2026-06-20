# NetSurf OpenOS 平台层移植记录

本目录记录 OpenOS 后续移植 NetSurf 的平台层拆分方案。

## 当前阶段

已新增宿主机可编译的最小平台层原型：

```text
ports/netsurf-openos/
├── include/openos_netsurf_platform.h
├── src/openos_netsurf_host.c
├── src/smoke.c
└── Makefile
```

该原型不包含 NetSurf 上游源码，目标是先固定 OpenOS 需要提供给浏览器内核的 OS/platform 能力边界。

## 已验证能力

- framebuffer surface 创建/销毁
- clip rect
- fill rect
- RGBA32 bitmap blit
- scroll/copy rect
- present 语义
- 事件轮询占位接口
- monotonic time
- 文件读取接口
- HTTP fetch 占位接口
- UTF-8 文本测量占位接口

验证命令：

```bash
cd ports/netsurf-openos
make smoke
```

## 与 OpenOS 内核/用户态 ABI 的对应关系

| 平台层接口 | OpenOS 当前能力 |
| --- | --- |
| surface fill/text/blit/scroll/present | `SYS_GUI_DRAW` |
| 窗口创建/销毁 | `SYS_GUI_CREATE_WINDOW` / `SYS_GUI_DESTROY_WINDOW` |
| 事件轮询 | `SYS_GUI_POLL_EVENT` |
| 时间 | `SYS_UPTIME_MS` |
| DNS/socket HTTP | 用户态 `openos_getaddrinfo/socket/connect/send/recv` |
| 字体测量 | `SYS_FONT_QUERY` |
| 配置/缓存目录 | `/home/browser/{cache,cookies,certs,downloads}` |

## 后续接入 NetSurf 的顺序

1. 在宿主机拉取 NetSurf 及依赖，先复用 framebuffer frontend 的构建方式。
2. 将 `openos_netsurf_platform.h` 映射到 NetSurf frontend callbacks。
3. 使用 OpenOS 用户态 ABI 实现 `openos_netsurf_openos.c`。
4. 先显示 HTTP 静态页面，再接入图片、HTTPS、cookie/cache。

## 非目标

当前阶段不尝试直接构建完整 NetSurf，也不引入上游第三方依赖到仓库。
