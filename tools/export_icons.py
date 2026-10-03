#!/usr/bin/env python3
"""Export the project icon master to platform sizes; normal builds need no Pillow."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "assets/branding"


def main() -> None:
    try:
        from PIL import Image
    except ImportError as error:
        raise SystemExit("Icon export requires Pillow: python -m pip install Pillow==12.3.0") from error
    master = Image.open(BRAND / "mdo-icon.png").convert("RGBA")
    if master.width != master.height:
        raise SystemExit("Icon master must be square")

    def png(path: Path, size: int) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        master.resize((size, size), Image.Resampling.LANCZOS).save(path, optimize=True)

    master.save(BRAND / "mdo.ico", sizes=[(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    web = ROOT / "app/web/assets"
    png(web / "mdo-icon.png", 128)
    png(web / "apple-touch-icon.png", 180)
    master.save(web / "favicon.ico", sizes=[(n, n) for n in (16, 32, 48)])
    res = BRAND / "android/res"
    for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        png(res / f"mipmap-{density}/ic_launcher.png", size)
    # Android masks and animates the full 108dp layer. The master already has
    # generous safe-area padding, so no artwork manipulation is needed.
    png(res / "drawable-nodpi/ic_launcher_foreground.png", 432)
    print("Exported Windows, Android and web icons from assets/branding/mdo-icon.png")


if __name__ == "__main__":
    main()
