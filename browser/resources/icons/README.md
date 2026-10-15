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
