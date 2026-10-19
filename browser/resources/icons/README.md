# Blynx icon assets

`blynx-mark.png` is a copy of the user's supplied eye logo, used on the built-in
new-tab page. The original download remains outside this repository; no private
filesystem path is part of the project documentation.

`blynx-taskbar.png` was generated with the built-in image_gen tool using that
image as a reference. The 16/32/64 PNGs are build-size derivatives for the native
Linux/Views resource pack; the desktop entry uses the full generated PNG.
The previous B artwork remains available as an earlier design.

The final redesign validation uses these local assets in the Linux/Views resource
patch and the user-local desktop entry. Exact generation prompt:

> Use case: logo-brand. Asset type: Blynx browser Linux taskbar/application icon.
> Reference image 1 is the user's Blynx eye emblem: off-white curved orbital eye
> surrounding a circular pupil, charcoal circular field. Generate a beautifully
> refined desktop icon based closely on that geometry, recognizable and crisp at
> 24px and 48px. Center one large charcoal circular medallion with the ivory eye
> and pupil, restrained cool silver edge lighting and subtle depth, premium calm
> finish. Maintain the eye's distinctive swept asymmetric curves. Transparent
> outside the circular medallion, no letters, text, additional symbols, frame,
> mockup, or checkerboard. Fill about 92% of the square canvas. Strong legibility,
> clean silhouette.

The identity follow-on adds `../blynx-eye.svg` and `../blynx-eye.icon`: matching
monochrome eye geometry for shared WebUI and native product glyphs. Existing
`cr-icon`/Views color plumbing supplies theme, selection and high-contrast colors;
these are not recolored provider logos or site favicons. `../product-icon.svg`
now uses the eye on a charcoal circular field rather than the earlier B artwork.
The generated taskbar PNG and its derivatives are unchanged.

The October 17 product-resource pass adds the remaining unscaled 16/24/48/64/
128/256 PNGs, Linux large/shortcut/XPM icons, light/dark 1x/2x wordmarks, monochrome
tray eye, standard/fallback SVGs, Windows application/document/PDF ICOs and macOS
ICNS. The shared dark-mode WebUI product mark uses `../blynx-eye-light.svg`.
`chromium-replacements.json` maps the 24 upstream resource paths to owned assets;
patch 0006 carries their reviewed binary/text replacements. Existing scaled
16/32 resources and the native product vector remain in patch 0004.

`scripts/generate_product_icons.py` regenerates these owned derivatives from
`blynx-taskbar.png` and the eye vectors, with Pillow, GI/librsvg/cairo and the
existing DejaVu Sans font and ImageMagick convert. It never writes the Chromium tree; refresh/review the
binary patch separately and use normal integration. The animation fallback is a
static Blynx mark. ICO/ICNS files are generated assets, not Windows/macOS runtime
or installer validation. External provider/store logos and functional incognito/
isolation glyphs retain their identities.

The version-page follow-on adds six `blynx-version-*` assets and
`version-replacements.json`. These replace the separate components-resource
wordmarks (171×32 and 342×64, light/dark) and 16/32 favicons used by
`blynx://version`; patch 0007 carries those replacements. The version page keeps
Chromium Authors attribution and reports the engine version truthfully.
