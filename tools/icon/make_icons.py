#!/usr/bin/env python3
"""App icons for iPad, Mac and Apple TV, made from the game's own art.

The art is the game's textures as it uploads them (GU_DUMP_TEXTURES=dir on a
run that reaches a level): Trane's orange X, a concrete wall, his tags. This
only cuts, scales, lights and layers them:
  icon1024.png               the iPad icon: a full-bleed square (the system
                             rounds it)
  AppIcon.icns               the Mac icon: the macOS shape, a rounded square
                             on the 1024 grid (824 px, 100 px margin) with its
                             shadow (also as icon-mac1024.png)
  Assets-tvos.xcassets       a layered (parallax) icon: the wall behind, the
                             X in front; the top shelf images: the wall with
                             tags along it, the X sprayed over them
  preview.png                all of them side by side

Usage: make_icons.py <texture dump dir> [out dir, default out/icons]  (Pillow)
"""
import json
import os
import random
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter

# The textures, by the content hash in their dump names.
X = '13f24cac8dcd0383'          # Trane's X (512x512, alpha)
CONCRETE = 'a69b83a81c72497d'   # a concrete wall (128x128)
STAINED = 'e53b53cd8db1037e'    # a stained concrete wall (256x128)
TAGS = ['0e06475e9089ad99', '054d72084b4a263a', '000d13f8f80ef8f2',
        '44491e04d5d532eb']     # tags (512x256, alpha)
INFO = {'author': 'xcode', 'version': 1}


def tex(dump, h):
    for f in os.listdir(dump):
        if f.endswith(h + '.bmp'):
            return Image.open(os.path.join(dump, f)).convert('RGBA')
    sys.exit(f'texture {h} not in {dump}')


def wall(dump, w, h, which=CONCRETE):
    """One wall texture over the whole w x h (no tiling: a repeat would
    show), softened as if out of focus, then darkened to night."""
    t = tex(dump, which).convert('RGB')
    k = max(w / t.width, h / t.height)
    t = t.resize((int(t.width * k) + 1, int(t.height * k) + 1), Image.BICUBIC)
    t = t.crop(((t.width - w) // 2, (t.height - h) // 2, (t.width - w) // 2 + w,
                (t.height - h) // 2 + h))
    t = t.filter(ImageFilter.GaussianBlur(max(w, h) / 900))
    t = ImageEnhance.Contrast(t).enhance(0.8)
    return ImageEnhance.Brightness(ImageEnhance.Color(t).enhance(0.5)).enhance(0.6)


def light(img, cx, cy, radius, floor=0.35):
    """A street light on the wall: full at (cx, cy), down to `floor` at
    `radius` and beyond."""
    w, h = img.size
    m = Image.new('L', (w, h), int(255 * floor))
    d = ImageDraw.Draw(m)
    steps = 64
    for i in range(steps):
        r = radius * (1 - i / steps)
        v = int(255 * (floor + (1 - floor) * (i / steps) ** 1.6))
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=v)
    m = m.filter(ImageFilter.GaussianBlur(radius / 12))
    return ImageChops.multiply(img.convert('RGB'), Image.merge('RGB', (m, m, m)))


def spray(base, art, box, angle=0.0, halo=0.35):
    """`art` painted onto `base` inside `box` (x, y, w, h): it takes the
    wall's grain (multiplied by the wall's light) and an overspray halo."""
    x, y, w, h = box
    a = art.resize((w, h), Image.LANCZOS)
    if angle:
        a = a.rotate(angle, Image.BICUBIC, expand=True)
        x -= (a.width - w) // 2
        y -= (a.height - h) // 2
    base = base.convert('RGBA')
    region = base.crop((x, y, x + a.width, y + a.height)).convert('L')
    grain = ImageEnhance.Contrast(region).enhance(1.6)
    grain = grain.point(lambda v: 222 + v * 33 // 255)  # 0.87 .. 1.0
    rgb = ImageChops.multiply(a.convert('RGB'), Image.merge('RGB', (grain,) * 3))
    rgb = ImageEnhance.Color(ImageEnhance.Brightness(rgb).enhance(1.08)).enhance(1.12)
    if halo:
        glow = a.getchannel('A').filter(ImageFilter.GaussianBlur(max(w, h) / 40))
        glow = glow.point(lambda v: int(v * halo))
        tint = Image.new('RGBA', a.size, a.resize((1, 1), Image.BOX).getpixel((0, 0))[:3] + (0,))
        tint.putalpha(glow)
        base.alpha_composite(tint, (x, y))
    painted = rgb.convert('RGBA')
    painted.putalpha(a.getchannel('A'))
    base.alpha_composite(painted, (x, y))
    return base


def shadow(layer, offset, blur, opacity):
    """A soft shadow of `layer`'s shape, for a layer floating above."""
    s = Image.new('RGBA', layer.size, (0, 0, 0, 0))
    s.putalpha(layer.getchannel('A').point(lambda v: int(v * opacity)))
    s = s.filter(ImageFilter.GaussianBlur(blur))
    out = Image.new('RGBA', layer.size, (0, 0, 0, 0))
    out.alpha_composite(s, offset)
    out.alpha_composite(layer)
    return out


def ios_icon(dump, size=1024):
    w = light(wall(dump, size, size), size * 0.5, size * 0.3, size * 1.05, floor=0.4)
    s = int(size * 0.72)
    return spray(w, tex(dump, X), ((size - s) // 2, (size - s) // 2 + size // 40, s, s),
                 angle=-4, halo=0.18).convert('RGB')


def squircle(size, n=5.0, ss=4):
    """Apple's rounded square, as a superellipse, antialiased."""
    big = size * ss
    m = Image.new('L', (big, big))
    px = m.load()
    r = big / 2
    for yy in range(big):
        ny = abs((yy + 0.5 - r) / r) ** n
        if ny >= 1:
            continue
        half = r * (1 - ny) ** (1 / n)
        for xx in range(int(r - half), int(r + half) + 1):
            if 0 <= xx < big:
                px[xx, yy] = 255
    return m.resize((size, size), Image.LANCZOS)


def mac_icon(dump):
    """The 1024 macOS icon: an 824 px rounded square with the art, its
    shadow below, on a transparent canvas."""
    body = 824
    art = ios_icon(dump, body).convert('RGBA')
    art.putalpha(squircle(body))
    canvas = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    canvas.alpha_composite(art, (100, 100))
    return shadow(canvas, (0, 12), 14, 0.45)


def icns(img, path):
    with tempfile.TemporaryDirectory() as d:
        iconset = os.path.join(d, 'AppIcon.iconset')
        os.mkdir(iconset)
        for s in (16, 32, 128, 256, 512):
            img.resize((s, s), Image.LANCZOS).save(f'{iconset}/icon_{s}x{s}.png')
            img.resize((2 * s, 2 * s), Image.LANCZOS).save(f'{iconset}/icon_{s}x{s}@2x.png')
        subprocess.run(['iconutil', '-c', 'icns', iconset, '-o', path], check=True)


def tv_layers(dump, w, h):
    """The Apple TV icon's layers: the lit wall with the X sprayed on it
    behind, a tag in front (the system moves them apart)."""
    back = light(wall(dump, w, h, STAINED), w * 0.5, h * 0.25, w * 0.75, floor=0.35)
    s = int(h * 0.95)
    back = spray(back, tex(dump, X), ((w - s) // 2, (h - s) // 2, s, s), angle=-6, halo=0.15)
    front = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    tw = int(w * 0.86)
    tag = tex(dump, TAGS[1]).resize((tw, tw // 2), Image.LANCZOS).rotate(4, Image.BICUBIC,
                                                                        expand=True)
    front.alpha_composite(tag, ((w - tag.width) // 2, (h - tag.height) // 2 + h // 30))
    return back.convert('RGB'), shadow(front, (0, h // 30), h // 25, 0.6)


def top_shelf(dump, w, h):
    """The wall across the top shelf, the tags sprayed along it, the X over
    the middle one."""
    img = light(wall(dump, w, h, STAINED), w * 0.3, h * 0.0, w * 0.75, floor=0.68)
    img = light(img, w * 0.75, h * 0.05, w * 0.6, floor=0.85)
    # (tag, centre x, centre y, width) as fractions of the shelf
    plan = [(TAGS[0], 0.16, 0.45, 0.25), (TAGS[1], 0.44, 0.58, 0.28),
            (TAGS[3], 0.86, 0.50, 0.22)]
    for t, cx, cy, tw in plan:
        tw = int(w * tw)
        img = spray(img, tex(dump, t), (int(w * cx - tw / 2), int(h * cy - tw / 4), tw, tw // 2),
                    angle=(cx - 0.5) * 8, halo=0.12)
    s = int(h * 0.5)
    img = spray(img, tex(dump, X), (int(w * 0.655 - s / 2), int(h * 0.36 - s / 2), s, s),
                angle=-8, halo=0.15)
    return img.convert('RGB')


def imageset(path, files, idiom='tv'):
    os.makedirs(path, exist_ok=True)
    images = []
    for scale, img in files:
        name = f'image@{scale}x.png'
        img.save(os.path.join(path, name))
        images.append({'filename': name, 'idiom': idiom, 'scale': f'{scale}x'})
    json.dump({'images': images, 'info': INFO}, open(os.path.join(path, 'Contents.json'), 'w'))


def imagestack(path, layers_by_scale):
    os.makedirs(path, exist_ok=True)
    names = ['Front', 'Back']
    for i, name in enumerate(names):
        lp = os.path.join(path, f'{name}.imagestacklayer')
        imageset(os.path.join(lp, 'Content.imageset'),
                 [(s, layers[i]) for s, layers in layers_by_scale])
        json.dump({'info': INFO}, open(os.path.join(lp, 'Contents.json'), 'w'))
    json.dump({'layers': [{'filename': f'{n}.imagestacklayer'} for n in names], 'info': INFO},
              open(os.path.join(path, 'Contents.json'), 'w'))


def tv_catalog(dump, out):
    cat = os.path.join(out, 'Assets-tvos.xcassets')
    brand = os.path.join(cat, 'App Icon & Top Shelf Image.brandassets')
    stack = lambda w, h: tv_layers(dump, w, h)[::-1]  # front first
    imagestack(os.path.join(brand, 'App Icon.imagestack'),
               [(1, stack(400, 240)), (2, stack(800, 480))])
    imagestack(os.path.join(brand, 'App Icon - App Store.imagestack'), [(1, stack(1280, 768))])
    imageset(os.path.join(brand, 'Top Shelf Image.imageset'),
             [(1, top_shelf(dump, 1920, 720)), (2, top_shelf(dump, 3840, 1440))])
    imageset(os.path.join(brand, 'Top Shelf Image Wide.imageset'),
             [(1, top_shelf(dump, 2320, 720)), (2, top_shelf(dump, 4640, 1440))])
    json.dump({'assets': [
        {'filename': 'App Icon - App Store.imagestack', 'idiom': 'tv',
         'role': 'primary-app-icon', 'size': '1280x768'},
        {'filename': 'App Icon.imagestack', 'idiom': 'tv', 'role': 'primary-app-icon',
         'size': '400x240'},
        {'filename': 'Top Shelf Image Wide.imageset', 'idiom': 'tv',
         'role': 'top-shelf-image-wide', 'size': '2320x720'},
        {'filename': 'Top Shelf Image.imageset', 'idiom': 'tv', 'role': 'top-shelf-image',
         'size': '1920x720'}], 'info': INFO},
        open(os.path.join(brand, 'Contents.json'), 'w'))
    json.dump({'info': INFO}, open(os.path.join(cat, 'Contents.json'), 'w'))


def main():
    dump = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', 'out', 'icons')
    os.makedirs(out, exist_ok=True)
    ios = ios_icon(dump)
    ios.save(os.path.join(out, 'icon1024.png'))
    mac = mac_icon(dump)
    mac.save(os.path.join(out, 'icon-mac1024.png'))
    icns(mac, os.path.join(out, 'AppIcon.icns'))
    tv_catalog(dump, out)

    # Preview: iPad icon as the system rounds it, the Mac icon, the TV icon
    # flattened, the top shelf.
    pv = Image.new('RGB', (2200, 1000), (24, 24, 28))
    r = ios.resize((400, 400), Image.LANCZOS).convert('RGBA')
    r.putalpha(squircle(400))
    pv.paste(r, (60, 60), r)
    m = mac.resize((480, 480), Image.LANCZOS)
    pv.paste(m, (500, 20), m)
    back, front = tv_layers(dump, 800, 480)
    flat = back.convert('RGBA')
    flat.alpha_composite(front)
    tv = flat.resize((800, 480), Image.LANCZOS)
    mask = Image.new('L', tv.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, *tv.size), radius=24, fill=255)
    pv.paste(tv, (1100, 60), mask)
    shelf = top_shelf(dump, 2320, 720).resize((1353, 420), Image.LANCZOS)
    pv.paste(shelf, (60, 560))
    pv.save(os.path.join(out, 'preview.png'))


if __name__ == '__main__':
    main()
