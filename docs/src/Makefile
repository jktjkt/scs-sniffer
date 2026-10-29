# Rebuilds the README illustrations in docs/ from the sources here.
# Needs python3, pdftocairo (poppler), rsvg-convert (librsvg) and magick (ImageMagick).
#
# board-*.svg and schematic.svg are vector overlays; the board ones reference the photos next to
# them, so open them from this directory.

all: ../board-top.jpg ../board-bottom.jpg ../schematic.svg ../schematic.pdf

board-top.svg: annotate_top.py photo-top.jpg
	python3 -I annotate_top.py photo-top.jpg $@

board-bottom.svg: annotate_bottom.py photo-bottom.jpg
	python3 -I annotate_bottom.py photo-bottom.jpg $@

../board-%.jpg: board-%.svg
	rsvg-convert $< | magick - -quality 88 $@

m5stack-unit-iso485-v1.1.svg: m5stack-unit-iso485-v1.1.pdf
	pdftocairo -svg $< $@

../schematic.svg: patch_schematic.py m5stack-unit-iso485-v1.1.svg
	python3 -I patch_schematic.py m5stack-unit-iso485-v1.1.svg $@

../schematic.pdf: ../schematic.svg
	rsvg-convert -f pdf $< -o $@

.PHONY: all
