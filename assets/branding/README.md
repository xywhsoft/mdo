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

## 图标重设计候选

`proposals/` 保存去除漏斗造型、保留 M 主体的新设计，尚未替换平台图标：

- `proposals/mdo-icon-square.png`：尺规 M，横向校准构件和垂直墨线。
- `proposals/mdo-icon-joinery.png`：榫卯 M，暖白构件以青铜接合件呼应文字标识。

两款均为正方形 PNG。使用内建 imagegen，以当前主图标作为重设计参考，
提示词见 [`proposals/prompts.md`](proposals/prompts.md)。

## 文字标识（已定版）

文字标识只包含横排艺术字“墨斗”，供与主图标组合使用。文件名沿用
`D:/GIT/home/host/xywhsoft/wwwroot/res/img` 的 `*_text.png` 形式。

- `mdo_text.png`：1774 × 887，墨色字身与青铜榫卯细节，适合浅色背景。
- `mdo_text_ivory.png`：2128 × 739，暖白雕刻字形，适合深色背景。

两款均为 RGBA PNG，已检查 Alpha 通道包含完全透明和完全不透明像素，
四角透明，保留笔画间的透明间隙。它们是独立文字素材，没有合入主图标或替换
应用中现有图标。组合时按文字实际可见边界对齐，保留原始宽高比。

文字通过内建 imagegen 生成；最终两款素材的提示词记录在
[`wordmark-prompts.md`](wordmark-prompts.md)。

## 主图标生成提示词

生成方式：内建 imagegen。

```text
Use case: logo-brand. Create one finished original app icon for a Chinese coding agent named 墨斗 (mdo). Asset: a square production icon master, suitable for Android launcher and Windows application icons. Visual idea: a single bold, intelligent abstract ink-dou / ink-line mark, inspired by the carpenter's traditional ink reservoir and taut ink string, with an understated geometric M feeling. It must read as one simple distinctive emblem at 24 pixels. Use warm ivory for the emblem on a solid near-black ink charcoal background (#20201e), matching a restrained dark/light coding workspace. Flat graphic / clean vector-like silhouette, premium editorial simplicity, confidently balanced shape and generous negative space. Center the entire emblem strictly inside the central 56% of the square so Android circle and squircle masks never crop the mark. Background fills the entire square edge to edge, no pre-rounded outer square, no border. No text, no letters, no wordmark, no Chinese glyph, no robot face, no brain, no gradients, no texture, no gloss, no shadows, no tiny details. Output just this one icon, not a mockup or a sheet of variants. 1024 by 1024 square.
```
