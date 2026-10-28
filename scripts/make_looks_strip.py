"""Joins the five bunny looks into one strip for the README.

Run from the repo root after rendering renders/bunny-look-*.png (see README, Showcase):

    pip install pillow
    python scripts/make_looks_strip.py
"""

from pathlib import Path

from PIL import Image

LOOKS = ["glass", "normal", "wireframe", "rose-gold", "gradient"]
GAP = 8  # pixels of background between panels


def main() -> None:
    renders = Path("renders")
    panels = [Image.open(renders / f"bunny-look-{name}.png").convert("RGB") for name in LOOKS]
    width = sum(p.width for p in panels) + GAP * (len(panels) - 1)
    height = max(p.height for p in panels)

    strip = Image.new("RGB", (width, height), (13, 13, 15))
    x = 0
    for panel in panels:
        strip.paste(panel, (x, 0))
        x += panel.width + GAP

    out = renders / "showcase-bunny-looks.jpg"
    strip.save(out, quality=92)
    print(f"Saved {out} ({width}x{height})")


if __name__ == "__main__":
    main()