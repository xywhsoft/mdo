# 墨斗图标

当前定版素材由用户提供，已从
`D:/GIT/home/host/xywhsoft/wwwroot/res/img` 导入本目录。五份原始 PNG 保留透明通道和原始字节；
构建和重新导出只依赖本仓库，不依赖该外部目录。

| 文件 | 尺寸 | 用途 |
| --- | --- | --- |
| `mdo_miniicon.png` | 720 × 720 | 简化 M，用于窗口、小尺寸 ICO、favicon 和页面侧栏 |
| `mdo_logo.png` | 1254 × 1254 | 方形 MDO，用于大尺寸 ICO、Android、欢迎和启动标志 |
| `mdo.png` | 2633 × 1128 | 图形与艺术字的横排组合标识 |
| `mdo_text.png` | 1774 × 887 | 定版艺术字 |
| `mdo_link.png` | 166 × 44 | 小尺寸横排链接标识 |

`mdo-icon.png` 是由方形素材导出的 512px 深色底预览，不再作为导出源。

- `mdo.ico`：Windows EXE、窗口和任务栏图标，共九帧。16–48px 使用简化 M，
  64、128、256px 使用方形 MDO；每帧均合成 `#20201e` 深色底，避免浅色桌面下失去对比。
- `android/res/`：五种密度的 launcher PNG，以及 Android 8+ 自适应图标。
  普通 launcher 合成深色底；自适应前景保留透明，将图案置于 108dp 图层的中央 66dp 区域，
  背景由系统使用同一深色提供，避免圆形和圆角方形裁切。
- `../../app/web/assets/`：favicon 和侧栏 `mdo-icon.png` 使用简化 M；
  `mdo-app-icon.png` 和 Apple touch icon 使用方形 MDO，均带深色底。

所有平台导出均已入库，正常构建不依赖 Pillow 或图片生成服务。
仅重新导出时需要 `python -m pip install Pillow==12.3.0`，然后执行
`python tools/export_icons.py`。Windows 重建宿主会传入此 ICO；
`--skip-host-build` 应只复用已经带此图标的 `.build/host`。

## 历史图标设计候选

`proposals/` 保存设计过程中的候选图。当前平台图标采用上面的定版素材：

- `proposals/mdo-icon-square.png`：尺规 M，横向校准构件和垂直墨线。
- `proposals/mdo-icon-joinery.png`：榫卯 M，暖白构件以青铜接合件呼应文字标识。
- `proposals/mdo-icon-mdo-rulers.png`：1254 × 1254 RGBA 透明素材；上方为普通 M，
  下方由直尺、半圆量角器组成 D，圆规与圆环组成 O。墨色主体搭配青铜圆规，适合浅色背景。

候选素材均为正方形 PNG。使用内建 imagegen，早期设计以当前主图标作为重设计参考，
提示词见 [`proposals/prompts.md`](proposals/prompts.md)。

## 文字标识（已定版）

当前 `mdo_text.png` 与其他四种规格一并从用户提供的目录导入，
横排组合版本为 `mdo.png`，小尺寸组合版本为 `mdo_link.png`。

- `mdo_text_ivory.png`：2128 × 739，保留的早期暖白雕刻方案。

原始文字素材为 RGBA PNG。组合时按实际可见边界对齐，并保留原始宽高比。

早期文字方案通过内建 imagegen 生成，其提示词记录在
[`wordmark-prompts.md`](wordmark-prompts.md)。

## 历史初版图标生成提示词

生成方式：内建 imagegen。

```text
Use case: logo-brand. Create one finished original app icon for a Chinese coding agent named 墨斗 (mdo). Asset: a square production icon master, suitable for Android launcher and Windows application icons. Visual idea: a single bold, intelligent abstract ink-dou / ink-line mark, inspired by the carpenter's traditional ink reservoir and taut ink string, with an understated geometric M feeling. It must read as one simple distinctive emblem at 24 pixels. Use warm ivory for the emblem on a solid near-black ink charcoal background (#20201e), matching a restrained dark/light coding workspace. Flat graphic / clean vector-like silhouette, premium editorial simplicity, confidently balanced shape and generous negative space. Center the entire emblem strictly inside the central 56% of the square so Android circle and squircle masks never crop the mark. Background fills the entire square edge to edge, no pre-rounded outer square, no border. No text, no letters, no wordmark, no Chinese glyph, no robot face, no brain, no gradients, no texture, no gloss, no shadows, no tiny details. Output just this one icon, not a mockup or a sheet of variants. 1024 by 1024 square.
```
