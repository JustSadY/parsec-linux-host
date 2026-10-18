.PHONY: source test runtime native run package srcinfo clean
source:
	python3 tools/prepare-source.py
runtime:
	$(MAKE) -C runtime all
test: runtime
	$(MAKE) -C runtime test
native:
	python3 tools/fetch-module.py
	python3 tools/build-native.py --module .cache/vendor/parsecd-150-105c.so --output .cache/native
run: native runtime
	bash tools/run-native.sh
package: source
	makepkg --force
srcinfo: source
	makepkg --printsrcinfo > .SRCINFO
clean:
	$(MAKE) -C runtime clean
