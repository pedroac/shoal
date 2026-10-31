#!/usr/bin/env python3
"""Draws the preview GIFs of Shoal's built-in transition types, in assets/transitions.

Each shows the type with its default options, from a daytime scene (A) to a dusk scene (B),
as an engine might draw it. The editor only shows them; the engine draws the real thing.

    python3 tools/make_transition_previews.py

Needs Pillow and numpy. Run it again after changing a built-in type, and commit the GIFs.
"""

from pathlib import Path

import numpy as np
from PIL import Image

W, H = 160, 90
HOLD = 6  # frames of each scene before and after
FRAMES = 22  # frames of the transition itself
FRAME_MS = 40
OUT = Path(__file__).resolve().parent.parent / "assets" / "transitions"
RNG = np.random.default_rng(7)
YS, XS = np.mgrid[0:H, 0:W].astype(np.float32)


def scene(sky_top, sky_bottom, ground, sun, sun_at, hills):
    """A simple landscape: a sky gradient, a sun, hills and the ground."""
    t = (YS / H)[..., None]
    image = np.array(sky_top, np.float32) * (1 - t) + np.array(sky_bottom, np.float32) * t
    sx, sy = sun_at
    image[(XS - sx) ** 2 + (YS - sy) ** 2 < 11**2] = sun
    for centre, height, colour in hills:
        top = H - height + ((XS - centre) / 38.0) ** 2 * height * 0.6
        image[YS > top] = colour
    image[YS > H * 0.82] = ground
    return image


A = scene((70, 140, 230), (170, 210, 250), (80, 160, 70), (255, 230, 90), (122, 24),
          [(40, 46, (90, 175, 85)), (115, 38, (70, 150, 70))])
B = scene((40, 30, 80), (240, 120, 80), (60, 45, 70), (255, 150, 90), (40, 52),
          [(30, 40, (95, 60, 105)), (125, 50, (75, 50, 95))])
BLACK = np.zeros_like(A)
WHITE = np.full_like(A, 255)


def mix(a, b, t):
    return a * (1 - t) + b * t


def where(mask, b, a):
    return np.where(mask[..., None], b, a)


def ease(t):
    return t * t * (3 - 2 * t)


def shift(image, dx, dy=0):
    """`image` moved by whole pixels, the rest left black."""
    out = np.zeros_like(image)
    dx, dy = int(round(dx)), int(round(dy))
    if abs(dx) >= W or abs(dy) >= H:
        return out
    xs = slice(max(dx, 0), W + min(dx, 0))
    xd = slice(max(-dx, 0), W + min(-dx, 0))
    ys = slice(max(dy, 0), H + min(dy, 0))
    yd = slice(max(-dy, 0), H + min(-dy, 0))
    out[ys, xs] = image[yd, xd]
    return out


def zoom(image, scale):
    """`image` scaled about its centre, cropped to the frame."""
    pil = Image.fromarray(image.astype(np.uint8))
    w, h = int(W * scale), int(H * scale)
    big = np.asarray(pil.resize((w, h), Image.BILINEAR), np.float32)
    x, y = (w - W) // 2, (h - H) // 2
    return big[y:y + H, x:x + W]


def pixelate(image, block):
    block = max(1, int(block))
    small = image[::block, ::block]
    return np.repeat(np.repeat(small, block, 0), block, 1)[:H, :W]


def fade(t):
    return mix(A, BLACK, t * 2) if t < 0.5 else mix(BLACK, B, t * 2 - 1)


def fade_in(t):
    return mix(BLACK, B, t)


def fade_out(t):
    return mix(A, BLACK, t) if t < 1 else B


def cross_dissolve(t):
    return mix(A, B, t)


def flashbang(t):
    if t < 0.1:
        return mix(A, WHITE, t / 0.1)
    if t < 0.25:
        return WHITE
    return mix(WHITE, B, (t - 0.25) / 0.75)


def screen_scroll(t):
    dx = ease(t) * W
    return shift(A, -dx) + shift(B, W - dx)


def slide(t):
    dx = W - ease(t) * W
    moved = shift(B, dx)
    return where(XS >= dx, moved, A)


def camera_push(t):
    pushed = zoom(A, 1 + ease(t))
    return pushed if t < 0.5 else mix(pushed, B, (t - 0.5) * 2)


def linear_wipe(t):
    return where(XS >= W * (1 - t), B, A)


def circle_wipe(t):
    radius = t * np.hypot(W, H) / 2
    return where((XS - W / 2) ** 2 + (YS - H / 2) ** 2 < radius**2, B, A)


# A pattern a designer might draw: diagonal stripes that open from their middles.
PATTERN = (np.abs(((XS + YS) % 24) - 12) / 12.0) * 0.8 + (XS / W) * 0.2


def pattern_mask(t):
    return where(PATTERN < t, B, A)


def pixelate_effect(t):
    block = 1 + (1 - abs(t * 2 - 1)) * 31
    return pixelate(A if t < 0.5 else B, block)


NOISE = RNG.random((H, W))


def dissolve(t):
    return where(NOISE < t, B, A)


BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
DITHER = BAYER[(YS // 2).astype(int) % 4, (XS // 2).astype(int) % 4]


def dither(t):
    return where(DITHER < t, B, A)


# The leaving scene in shards (a grid of tiles), each falling and drifting on its own.
TILES = [(x, y, RNG.uniform(0, 0.4), RNG.uniform(-12, 12))
         for y in range(0, H, 15) for x in range(0, W, 20)]


def shatter(t):
    out = B.copy()
    for x, y, delay, drift in TILES:
        u = max(0.0, (t - delay) / (1 - delay))
        dy, dx = u * u * H * 1.4, u * drift
        tile = np.zeros_like(A)
        tile[y:y + 15, x:x + 20] = A[y:y + 15, x:x + 20]
        mask = np.zeros((H, W), bool)
        mask[y:y + 15, x:x + 20] = True
        moved, moved_mask = shift(tile, dx, dy), shift(mask[..., None].astype(np.float32), dx, dy)
        out = where(moved_mask[..., 0] > 0.5, moved, out)
    return out


def glitch(t):
    image = (A if t < 0.5 else B).copy()
    strength = (1 - abs(t * 2 - 1)) * 0.5 * 2  # the default intensity, 0.5, at its peak
    out = image.copy()
    for _ in range(int(strength * 10)):
        y = RNG.integers(0, H - 4)
        h = RNG.integers(2, 10)
        out[y:y + h] = shift(image, RNG.integers(-24, 24) * strength)[y:y + h]
    out[..., 0] = shift(out, int(4 * strength))[..., 0]
    out[..., 2] = shift(out, -int(4 * strength))[..., 2]
    return out


EFFECTS = {
    "fade": fade,
    "fade_in": fade_in,
    "fade_out": fade_out,
    "cross_dissolve": cross_dissolve,
    "flashbang": flashbang,
    "screen_scroll": screen_scroll,
    "slide": slide,
    "camera_push": camera_push,
    "linear_wipe": linear_wipe,
    "circle_wipe": circle_wipe,
    "pattern_mask": pattern_mask,
    "pixelate": pixelate_effect,
    "dissolve": dissolve,
    "dither": dither,
    "shatter": shatter,
    "glitch": glitch,
}


def main():
    for name, effect in EFFECTS.items():
        frames = [A] * HOLD
        frames += [effect(i / (FRAMES - 1)) for i in range(FRAMES)]
        frames += [B] * HOLD
        images = [Image.fromarray(np.clip(f, 0, 255).astype(np.uint8)) for f in frames]
        images[0].save(OUT / f"{name}.gif", save_all=True, append_images=images[1:],
                       duration=FRAME_MS, loop=0, optimize=True)
        print(OUT / f"{name}.gif")


if __name__ == "__main__":
    main()
