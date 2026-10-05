#!/usr/bin/env python3
"""Export approved full and miniature artwork; normal builds need no Pillow."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "assets/branding"


def main() -> None:
    try:
        from PIL import Image
    except ImportError as error:
        raise SystemExit("Icon export requires Pillow: python -m pip install Pillow==12.3.0") from error
    master = Image.open(BRAND / "mdo_logo.png").convert("RGBA")
    miniature = Image.open(BRAND / "mdo_miniicon.png").convert("RGBA")
    if any(image.width != image.height for image in (master, miniature)):
        raise SystemExit("Full and miniature icon masters must be square")

    def square(image: Image.Image, size: int, *, opaque: bool = True) -> Image.Image:
        result = image.resize((size, size), Image.Resampling.LANCZOS)
        if opaque:
            # Warm-white transparent artwork needs contrast on light desktops.
            background = Image.new("RGBA", (size, size), "#20201e")
            background.alpha_composite(result)
            result = background
        return result

    def png(path: Path, image: Image.Image, size: int) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        square(image, size).save(path, optimize=True)

    def ico(path: Path, sizes: tuple[int, ...], *, mini_only: bool = False) -> None:
        # Supply an explicit source for every frame. A large MDO thumbnail loses
        # detail in a window title bar; those frames use the approved simple M.
        frames = [square(miniature if mini_only or size <= 48 else master, size)
                  for size in sizes]
        frames[-1].save(path, sizes=[(size, size) for size in sizes],
                        append_images=frames[:-1])

    png(BRAND / "mdo-icon.png", master, 512)
    ico(BRAND / "mdo.ico", (16, 20, 24, 32, 40, 48, 64, 128, 256))
    web = ROOT / "app/web/assets"
    png(web / "mdo-icon.png", miniature, 128)
    png(web / "mdo-app-icon.png", master, 192)
    png(web / "apple-touch-icon.png", master, 180)
    ico(web / "favicon.ico", (16, 32, 48), mini_only=True)
    res = BRAND / "android/res"
    for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        png(res / f"mipmap-{density}/ic_launcher.png", master, size)
    # The 108dp adaptive layer extends beyond the system mask. Put the artwork
    # in its central 66dp area and let Android provide the dark background.
    foreground = Image.new("RGBA", (432, 432))
    foreground.alpha_composite(square(master, 264, opaque=False), (84, 84))
    foreground.save(res / "drawable-nodpi/ic_launcher_foreground.png", optimize=True)
    print("Exported Windows, Android and web icons from approved mdo_logo/mdo_miniicon")


if __name__ == "__main__":
    main()
