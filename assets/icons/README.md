# Export destination icons

Brand marks for the export destination list, plus generic glyphs for the
destinations that are not a brand.

## Source and licence

Brand icons come from **[Simple Icons](https://simpleicons.org)** (Iconify prefix
`simple-icons`), version **16.30.0**, dataset dated 2026-09-07, retrieved through
`https://api.iconify.design/simple-icons/<name>.svg`.

- **Licence: CC0 1.0 Universal** (SPDX `CC0-1.0`). No attribution is required.
- The files are monochrome: a single path with `fill="currentColor"`, which is what
  `theme::tintIcons()` expects, so each icon takes a theme colour token like every
  other glyph in `assets/icons/`.

Generic glyphs (`dest-*.svg`) come from **[Material Design Icons](https://pictogrammers.com/library/mdi/)**
(Iconify prefix `mdi`), **Apache-2.0**. One brand set cannot cover both the 22
brands and the generic concepts (a chat bubble, a paperclip, a film strip), and no
single Iconify collection has both; two sets is the smallest honest answer.

## Trademark

CC0 waives **copyright** in these files. It does not waive **trademark**: every
brand mark remains the property of its owner, and Simple Icons' own licence says so
in section 4(a). Using a mark to label the export destination it names is ordinary
descriptive use. Do not present the application as affiliated with or endorsed by
those platforms, and do not use these marks as the application's own icon.

## Adding one

Fetch it, keep it monochrome, and reference it from the destination's `icon`
attribute in `assets/export/ExportTargets.txt`:

    curl -sfL https://api.iconify.design/simple-icons/<name>.svg -o assets/icons/brand-<name>.svg

Add the file to `assets/assets.qrc` as well, or it will not be found at runtime.
Naming traps in Simple Icons: X is `x` (there is no `twitter`), Weibo is
`sinaweibo`, OBS is `obsstudio`, Nico Nico Douga is `niconico`.

Douyin has **no mark in Simple Icons** (checked 2026-09-11 against the whole
collection: `douyin` does not exist, `douban` does). Its destination row therefore
uses the generic `dest-film` glyph. Pulling a Douyin mark from another Iconify
collection would put a second visual language on the same list next to the 30 marks
drawn from this one, so it is left generic deliberately.

