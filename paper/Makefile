.PHONY: figures pdf clean

figures: figures/farm_zoom.png figures/oracle_magma.png figures/rl_curve.pdf

figures/farm_zoom.png: ../docs/assets/zoom_farm.gif
	mkdir -p figures
	ffmpeg -hide_banner -loglevel error -y -i $< \
		-vf "select=eq(n\,70)" -frames:v 1 $@

figures/oracle_magma.png: ../demos/pixel_match_sbs.mp4
	mkdir -p figures
	ffmpeg -hide_banner -loglevel error -y -ss 2.2 -i $< -frames:v 1 $@

figures/rl_curve.pdf: render_rl_curve.py data/chain_curve_pin1_1h.npy
	uv run --no-project --with numpy,matplotlib python render_rl_curve.py

pdf: figures
	tectonic main.tex

clean:
	rm -f main.aux main.bbl main.blg main.log main.out main.pdf
