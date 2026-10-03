# 墨斗图标

墨色底与暖白色墨仓、墨线组合，轮廓呼应 mdo 的 M。原图通过内建 imagegen
生成，位于 `mdo-icon.png`（1254 × 1254）；平台导出不改变图案，仅调整尺寸与编码。

- `mdo.ico`：Windows EXE、窗口和任务栏图标，16–256px 共九个尺寸。
- `android/res/`：五种密度的 launcher PNG，以及 Android 8+ 自适应图标。
  原图留有安全边距，由系统完成圆形、圆角方形等裁切；foreground 是完整色彩图层。
- `../../app/web/assets/`：浏览器 favicon、Apple touch icon 和页面标志。

所有平台导出均已入库，正常构建不依赖 Pillow 或图片生成服务。
仅重新导出时需要 `python -m pip install Pillow==12.3.0`，然后执行
`python tools/export_icons.py`。Windows 重建宿主会传入此 ICO；
`--skip-host-build` 应只复用已经带此图标的 `.build/host`。

原图生成提示词（内建 imagegen）：

```text
Use case: logo-brand. Create one finished original app icon for a Chinese coding agent named 墨斗 (mdo). Asset: a square production icon master, suitable for Android launcher and Windows application icons. Visual idea: a single bold, intelligent abstract ink-dou / ink-line mark, inspired by the carpenter's traditional ink reservoir and taut ink string, with an understated geometric M feeling. It must read as one simple distinctive emblem at 24 pixels. Use warm ivory for the emblem on a solid near-black ink charcoal background (#20201e), matching a restrained dark/light coding workspace. Flat graphic / clean vector-like silhouette, premium editorial simplicity, confidently balanced shape and generous negative space. Center the entire emblem strictly inside the central 56% of the square so Android circle and squircle masks never crop the mark. Background fills the entire square edge to edge, no pre-rounded outer square, no border. No text, no letters, no wordmark, no Chinese glyph, no robot face, no brain, no gradients, no texture, no gloss, no shadows, no tiny details. Output just this one icon, not a mockup or a sheet of variants. 1024 by 1024 square.
```
