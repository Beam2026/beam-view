#!/bin/sh

# Regenerates upstream Moonlight's installer icons. It does NOT generate Beam's.
#
# BEAM: read this before assuming the file below is the icon you are looking for.
#
# There are three icons in this repo and this script produces none of the two that matter:
#
#   app/beam.ico       the executable icon (`RC_ICONS` in app.pro) -- what Explorer shows
#   app/res/beam.png   the window icon (`setWindowIcon`, and SDL's `SDL_SetWindowIcon`)
#                      -- what Task Manager shows beneath the process, and Alt+Tab
#   app/moonlight.ico  upstream's WiX bundle icon -- generated here, not shipped by Beam
#
# Beam's two are **copies from the Beam repo**, which owns the artwork:
#
#   app/beam.ico      <- Beam/desktop/src-tauri/icons/icon.ico       (6 sizes, 16..256)
#   app/res/beam.png  <- Beam/desktop/src-tauri/icons/128x128.png
#
# Both are byte-identical to their source as of 2026-09-22. To refresh them, copy from
# there -- do **not** rasterise beam.ico out of res/beam.png. That PNG is 128x128 and the
# committed .ico has a 256x256 entry, so "regenerating" it would quietly downgrade the icon
# Windows shows at the largest size.
#
# The two `convert` calls below serve `wix/MoonlightSetup/Bundle.wxs`, which is upstream's
# installer. Beam does not use it: Beam ships this program as a bundled engine and has its
# own installer. They are left working rather than deleted so a rebase does not fight over
# a file upstream still maintains -- which is also why res/moonlight.svg stays on disk even
# though it is no longer compiled into the binary.
#
# `convert` here means ImageMagick. On Windows, `convert` on PATH is
# C:\Windows\System32\convert.exe, the FAT-to-NTFS filesystem converter, which will not do
# what you want -- run this from a shell where ImageMagick comes first, or use `magick`.

# The ImageMagick conversion tool doesn't seem to always generate
# ICO files with background transparency properly. Please validate
# that the output has a transparent background.

convert -density 256 -background none -define icon:auto-resize ../app/res/moonlight.svg ../app/moonlight.ico
convert -density 256 -background none -size 64x64 ../app/res/moonlight.svg ../app/moonlight_wix.png

echo IMPORTANT: Validate the icon has a transparent background before committing!
echo NOTE: this regenerated upstream WiX icons only. Beam icons are copies - see the header.
