PROJECT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
CODE := $(PROJECT)/experiments/native-patch
LAB := $(PROJECT)/.cache/native-patch
MODULE ?= $(HOME)/.parsec/parsecd-150-105c.so
.PHONY: build patch check gui desktop
build:
	mkdir -p "$(LAB)"
	cc -Os -fPIC -fcf-protection=branch -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -c "$(CODE)/capabilities.c" -o "$(LAB)/capabilities.o"
	ld -nostdlib -T "$(CODE)/capabilities.ld" "$(LAB)/capabilities.o" -o "$(LAB)/capabilities.elf"
	objcopy -O binary --only-section=.replacement "$(LAB)/capabilities.elf" "$(LAB)/capabilities.bin"
	cc -O2 -Wall -Wextra "$(CODE)/probe.c" -ldl -o "$(LAB)/probe"
patch: build
	python3 "$(CODE)/patch.py" "$(MODULE)"
check: patch
	python3 "$(CODE)/check.py" "$(MODULE)"
gui: patch
	python3 "$(CODE)/gui-smoke.py" "$(LAB)/parsecd-150-105c-rebuilt.so" --online
desktop: check
	python3 "$(CODE)/run-desktop.py"
