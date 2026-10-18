.PHONY: source test native run package srcinfo
source:
	python3 tools/prepare-source.py
test:
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=app python3 -m unittest discover -s tests -v
native:
	python3 tools/fetch-module.py
	python3 tools/build-native.py --module .cache/vendor/parsecd-150-105c.so --output .cache/native
run: native
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=app python3 tools/run-native.py
package: source
	makepkg --force
srcinfo: source
	makepkg --printsrcinfo > .SRCINFO
