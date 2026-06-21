# OpenOS 浏览器引擎路线：Chromium Only

OpenOS 浏览器目标只接受真实 Chromium/Chrome 引擎路线：

```text
Chromium Content + Blink + V8 + Skia
```

## 明确废弃

以下路线不再作为 OpenOS 最终浏览器方向：

- NetSurf
- Dillo
- Links/Lynx
- QuickJS + 自研 DOM demo
- 任何只显示 HTTP/HTML 文本的自研 `/bin/chromium` demo

这些组件可以作为历史实验参考，但不能宣称为真实 Chrome/Chromium 引擎。

## 当前事实

当前 `/bin/browser` 和 `/bin/chromium` 仍是 OpenOS 用户态 demo，用于验证 GUI、HTTP、线程、SDK 和系统调用能力；它们不是 Chrome 引擎。

## 近期唯一主线

1. 固定 Chromium 上游源码获取入口和版本。
2. 建立 OpenOS Chromium GN/toolchain 骨架。
3. 先接官方 Skia 软件 raster。
4. 再接官方 V8 jitless shell。
5. 再接 Blink/content_shell 单进程软件渲染链路。
6. 最终用真实 Chromium Content shell 替换当前 `/bin/chromium` demo。
