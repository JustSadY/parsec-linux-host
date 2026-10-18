.PHONY: source test preview package srcinfo
source:
	python3 tools/prepare-source.py
test:
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=app QT_QPA_PLATFORM=offscreen python3 -m unittest discover -s tests -v
preview:
	python3 tools/run-dev.py --preview
package: source
	makepkg --force
srcinfo: source
	makepkg --printsrcinfo > .SRCINFO
