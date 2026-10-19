#!/usr/bin/env python3
"""Regenerate owned Blynx product assets; does not modify Chromium.

Requires Pillow, GI/librsvg/cairo and the existing DejaVu Sans font and ImageMagick convert.
Refresh the reviewed binary patch separately before integrating/rebuilding.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json
import subprocess
root=Path(__file__).resolve().parents[1]
owned=root/'browser/resources/icons'
image=Image.open(owned/'blynx-taskbar.png').convert('RGBA')
manifest={}
def install(target, asset):
    manifest[target]=str(asset.relative_to(root/'browser/resources'))
for size in (16,24,48,64,128,256):
    asset=owned/f'blynx-product-{size}.png'
    image.resize((size,size), Image.Resampling.LANCZOS).save(asset)
    install(f'chrome/app/theme/chromium/product_logo_{size}.png',asset)
    linux=f'chrome/app/theme/chromium/linux/product_logo_{size}.png'
    if size in (24,48,64,128,256):install(linux,asset)
for name in ('product_logo.svg','product_logo_animation.svg'):
    install('chrome/app/theme/chromium/'+name,root/'browser/resources/product-icon.svg')
# Raster wordmarks keep the exact GRIT resource dimensions. Fonts are baked in.
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
for scale in (1,2):
 for white in (False,True):
    canvas=Image.new('RGBA',(97*scale,22*scale))
    canvas.alpha_composite(image.resize((22*scale,22*scale),Image.Resampling.LANCZOS))
    ImageDraw.Draw(canvas).text((26*scale,-1*scale),'Blynx',font=ImageFont.truetype(font,18*scale),fill=('#f4f1e9' if white else '#202124'))
    suffix='_white' if white else ''
    asset=owned/f'blynx-wordmark-{scale}x{suffix}.png';canvas.save(asset)
    install(f'chrome/app/theme/default_{100*scale}_percent/chromium/product_logo_name_22{suffix}.png',asset)
asset=owned/'blynx-app.ico';image.save(asset,format='ICO',sizes=[(s,s) for s in (16,24,32,48,64,128,256)])
install('chrome/app/theme/chromium/win/chromium.ico',asset)
for kind in ('doc','pdf'):
 canvas=Image.new('RGBA',(256,256));draw=ImageDraw.Draw(canvas)
 draw.rounded_rectangle((30,6,226,250),radius=16,fill='#f4f1e9',outline='#767b84',width=4)
 canvas.alpha_composite(image.resize((160,160),Image.Resampling.LANCZOS),(48,34))
 draw.text((68,200),kind.upper(),font=ImageFont.truetype(font,32),fill='#202124')
 asset=owned/f'blynx-{kind}.ico';canvas.save(asset,format='ICO',sizes=[(s,s) for s in (16,24,32,48,64,128,256)])
 install(f'chrome/app/theme/chromium/win/chromium_{kind}.ico',asset)
asset=owned/'blynx-app.icns';image.resize((1024,1024),Image.Resampling.LANCZOS).save(asset,format='ICNS')
install('chrome/app/theme/chromium/mac/app.icns',asset)
# The tray mask uses an alpha-only monochrome version of the eye vector.
import gi,cairo
gi.require_version('Rsvg','2.0')
from gi.repository import Rsvg
handle=Rsvg.Handle.new_from_data((root/'browser/resources/blynx-eye.svg').read_bytes().replace(b'currentColor',b'#ffffff'))
surface=cairo.ImageSurface(cairo.FORMAT_ARGB32,22,22)
rect=Rsvg.Rectangle();rect.x=0;rect.y=0;rect.width=22;rect.height=22
handle.render_document(cairo.Context(surface),rect)
asset=owned/'blynx-tray-22.png';surface.write_to_png(str(asset));install('chrome/app/theme/chromium/product_logo_22_mono.png',asset)
asset=root/'browser/resources/blynx-eye-light.svg'
asset.write_text((root/'browser/resources/blynx-eye.svg').read_text().replace('currentColor','#f4f1e9').replace('<svg ', '<svg width="24" height="24" ', 1))
install('ui/webui/resources/images/chrome_logo_dark.svg',asset)
asset=owned/'blynx-product-32.xpm'
subprocess.run(['convert',str(owned/'blynx-taskbar-32.png'),'-colors','64',str(asset)],check=True)
install('chrome/app/theme/chromium/linux/product_logo_32.xpm',asset)
(owned/'chromium-replacements.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
print('Updated',len(manifest),'product icon resources')

# Version UI uses a separate components resource family, including its wordmark.
version_manifest = {}
for scale in (1, 2):
    for white in (False, True):
        canvas = Image.new('RGBA', (171 * scale, 32 * scale))
        canvas.alpha_composite(image.resize((32 * scale, 32 * scale), Image.Resampling.LANCZOS))
        ImageDraw.Draw(canvas).text(
            (43 * scale, -2 * scale), 'Blynx',
            font=ImageFont.truetype(font, 29 * scale),
            fill='#f4f1e9' if white else '#202124')
        suffix = '_white' if white else ''
        asset = owned / f'blynx-version-wordmark-{scale}x{suffix}.png'
        canvas.save(asset)
        target = f'components/resources/default_{scale * 100}_percent/chromium/product_logo{suffix}.png'
        version_manifest[target] = str(asset.relative_to(root / 'browser/resources'))
    size = 16 * scale
    asset = owned / f'blynx-version-favicon-{scale}x.png'
    image.resize((size, size), Image.Resampling.LANCZOS).save(asset)
    target = f'components/resources/default_{scale * 100}_percent/chromium/favicon_product.png'
    version_manifest[target] = str(asset.relative_to(root / 'browser/resources'))
(owned / 'version-replacements.json').write_text(json.dumps(version_manifest, indent=2, sort_keys=True) + '\n')
