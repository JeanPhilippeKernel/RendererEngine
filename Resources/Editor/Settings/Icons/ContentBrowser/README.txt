Content browser thumbnail artwork

Source: Figma file r17DX8aJRKtHEpiV8iAyA8, Asset Tile master 234:2858.
Exported 2026-10-18. The nine original SVG files are the exact downloaded assets.
Their 38px geometry and ~1.4px strokes are deliberately not the 16px toolbar
font enlarged to thumbnail size.

Folder 234:2635; Box 234:2643; Image 234:2653; Map 234:2663;
Workflow 234:2673 (also Shader); Code 234:2693; Square 234:2702.
Empty folder: FolderOpen 238:2751; No results: Search 238:2780.

engine.svg is an engine-owned outline adaptation of ../AppIconWhite.png for .zescene
files. It preserves the opposing corners and curved diagonal rails, with the same
38px canvas, ~1.4px rounded strokes and runtime tint as the existing library.
The top-left application logo is unchanged. This is not a Lucide or Figma export.

thumbnails.png contains 152px single-channel grayscale coverage masks in this order:
folder, box, image, map, workflow, code, square, folder-open, search, engine.
Grayscale stores alpha coverage, not visible color. The engine copies these rows
into the existing font atlas's alpha plane and applies its color token. Decoding
uses one byte per pixel, with no unused RGB or channel-conversion buffer.
There is no runtime SVG dependency and no separate GPU texture per icon/tile.

Regenerate on macOS, from the repository root:
swift Scripts/bake_content_icons.swift Resources/Editor/Settings/Icons/ContentBrowser

Original icon licenses: ../../Fonts/Lucide/LICENSE.txt (Lucide ISC and Feather MIT).
The custom engine mark follows the repository's license.
