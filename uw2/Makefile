# UW2's build driver (uw2/ of Underworld Exhumed). The work is done in headless DOS by the tools in tools/; this file
# only names the entry points. tools/uw2.py holds the gate's logic.
#
#   make / make game   modding build (tools/link.py --mod), prints the EXE's path
#   make exact         exact link, compared with your UW2.EXE (tools/exediff.py)
#   make check         the full gate; recompiles only sources changed since they last passed
#   make check-all     the full gate, recompiling every source
#   make boot          boot the modding build to the intro and screenshot it
#   make setup         toolchain, Python venv, npm packages, emu2 and Exhume (idempotent)
#   make setup-exhume  only find or fetch Exhume, whose runtime the port and the gate compile
#                      ($EXHUME, the submodule ../exhume, .exhume or ~/Exhume; tools/setup-exhume.sh)
#   make setup-emu2    only build emu2, the fast DOS for the toolchain (tools/setup-emu2.sh)
#   make setup-sound   fetch the port's OPL emulator, Nuked OPL3 (tools/setup-sound.sh)
#   make setup-libs    build SDL3 and libmt32emu from source into tools/libs, where no package
#                      has them (Linux; tools/setup-libs.sh)
#   make setup-port    only what the native port needs, per OS (tools/setup-port.sh, and
#                      Exhume): no Borland toolchain, DOS or game data
#   make hooks         install the git pre-push hook that runs make test
#   make port-check    compile the C for the host, compile only (docs/PORT.md, Milestone 1)
#   make port          compile the C for the host and link it with the stubs: build/port/uw2port
#   make port-debug    the same with -g and UBSan's -fsanitize=null: build/port-debug/uw2port
#   make port-release  the build the release packages use: build/port-release/uw2port, with Nuked
#                      OPL3 a shared library beside it (tools/portbuild.py --release)
#   make web           the WebAssembly build (tools/portbuild.py --web, with emsdk in ~/emsdk and
#                      the libraries SETUP_WEB=1 setup-libs.sh built in tools/libs-web): build/web
#                      for the page, build/web-node/uw2port for Node.js
#   make webcheck      make web, then every session against its golden in the Node.js build
#                      (an hour's --exit-after, EXHUME_EXIT_AFTER: it runs far slower)
#   make package       package build/port-release for this OS into build/dist (tools/package.py)
#   make test          check + port + routine fuzzing (quick) + every session against its golden
#                      (docs/BUILDING.md, Testing); the pre-push hook runs it
#   make test-full     check + both port builds + goldens regenerated from DOS (twice each) +
#                      the sessions in the port and the UBSan build + ailcheck + deep fuzzing +
#                      the coverage report
#   make verify        only the sessions against their goldens, in the port (tools/replay.py verify)
#   make blackbox      the black box: a player's sessions recorded, staged and pruned as a
#                      player's build does it (Exhume's tools/blackboxcheck.py, as make test runs it)
#   make golden        regenerate every session's golden from DOS (tools/replay.py golden)
#   make fuzz          the routine fuzzing, quick (tools/fuzzasm.py; FUZZ=--deep for the long run)
#   make vectors       the test vectors: UW2.EXE's routines on synthetic inputs, written to vectors/
#                      and checked against the port's C (Exhume's tools/vectors.py with
#                      tools/vector_targets.py and tools/vectorhost-uw2.c; vectors/README.md);
#                      VECTORS=--check only checks that they regenerate byte-identical
#   make coverage      the port's coverage over the sessions and the fuzzing: docs/COVERAGE.md
#
# make setup needs the Borland disk images the first time:
#   make setup TC_DISKS="/path/to/Turbo C++ 1.01" TASM_DISKS="/path/to/Turbo Assembler 2.0"

PY := $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)
# Exhume's checkout, found as tools/exhume.py finds it ($EXHUME, else the submodule ../exhume,
# else .exhume, else ~/Exhume)
EXHUME ?= $(firstword $(wildcard ../exhume/tools/gate.py .exhume/tools/gate.py))
ifeq ($(EXHUME),)
# a repository that holds Exhume as a submodule uses it, never another checkout: an empty one is
# an error (but not for the setup targets, which fill it)
ifneq ($(shell grep -s 'path = exhume' ../.gitmodules),)
ifeq ($(filter setup%,$(MAKECMDGOALS)),)
$(error Exhume: the submodule ../exhume is empty: run git submodule update --init (or make setup-exhume))
endif
EXHUME := ../exhume
else
EXHUME := $(HOME)/Exhume
endif
else
EXHUME := $(patsubst %/tools/gate.py,%,$(EXHUME))
endif
TC_DISKS ?=
TASM_DISKS ?=

.PHONY: game exact check check-all boot setup setup-emu2 setup-exhume setup-sound setup-libs setup-port hooks port-check port port-debug \
        port-release web webcheck package help \
        test test-full verify blackbox golden fuzz vectors coverage
.DEFAULT_GOAL := game

game:
	@$(PY) tools/uw2.py game

exact:
	@$(PY) tools/uw2.py exact

check:
	@$(PY) tools/uw2.py check

check-all:
	@$(PY) tools/uw2.py check --all

boot:
	@$(PY) tools/uw2.py boot

setup:
	@if [ -f TC/TCC.EXE ] && [ -f TC/OVERLAY.LIB ] && [ "$$(md5 -q TC/TCC.EXE 2>/dev/null || md5sum TC/TCC.EXE | cut -d' ' -f1)" = db4c0704f7091b8d875be038bee2bf1e ]; then \
	  echo "TC: Turbo C++ 1.01 already in TC/"; \
	elif [ -n "$(TC_DISKS)" ]; then sh tools/setup-tc.sh "$(TC_DISKS)"; \
	else echo "TC/ is missing: make setup TC_DISKS=DIR (the directory holding Disk01.img..Disk04.img)"; exit 1; fi
	@if [ -f TASM/TASM.EXE ] && [ "$$(md5 -q TASM/TASM.EXE 2>/dev/null || md5sum TASM/TASM.EXE | cut -d' ' -f1)" = b68a63d6a94672910d4149fad4c18f00 ]; then \
	  echo "TASM: Turbo Assembler 2.0 already in TASM/"; \
	elif [ -n "$(TASM_DISKS)" ]; then sh tools/setup-tasm.sh "$(TASM_DISKS)"; \
	else echo "TASM/ is missing: make setup TASM_DISKS=DIR (the directory holding Disk01.img)"; exit 1; fi
	@[ -x .venv/bin/python ] || python3 -m venv .venv
	@.venv/bin/python -c "import iced_x86, unicorn" 2>/dev/null || .venv/bin/pip install -q -r tools/requirements.txt
	@echo "Python: .venv with iced-x86 and unicorn (tools/requirements.txt)"
	@[ -d node_modules/dos-mcp ] || npm install
	@echo "Node: dos-mcp installed"
	@sh tools/setup-emu2.sh || echo "emu2: not built; the toolchain runs in DOSBox-X if installed (brew install dosbox-x), else js-dos"
	@echo "DOS for the toolchain: $$(node tools/dosbackend.mjs) (docs/BUILDING.md, Choosing the DOS)"
	@sh tools/setup-sound.sh || echo "nuked-opl3: not fetched; the port builds without FM music"
	@sh tools/setup-exhume.sh
	@[ -f "$${UW2_EXE:-$$HOME/UWGOG/UW2/UW2.EXE}" ] && echo "UW2.EXE: $${UW2_EXE:-$$HOME/UWGOG/UW2/UW2.EXE}" \
	  || { echo "UW2.EXE not found: put the game at ~/UWGOG/UW2 or set UW2_EXE"; exit 1; }

setup-emu2:
	@sh tools/setup-emu2.sh

setup-exhume:
	@sh tools/setup-exhume.sh

setup-sound:
	@sh tools/setup-sound.sh

setup-libs:
	@sh tools/setup-libs.sh

setup-port:
	@sh tools/setup-port.sh
	@sh tools/setup-exhume.sh

# git push runs this game's make test first (Exhume's tools/install-hooks.sh: an entry for uw2/ in the
# repository's one hook, beside uw1's when that is installed too)
hooks:
	@sh $(EXHUME)/tools/install-hooks.sh . "make test"

port-check:
	@$(PY) tools/portcheck.py

port:
	@$(PY) tools/portbuild.py

port-debug:
	@$(PY) tools/portbuild.py --debug

port-release:
	@$(PY) tools/portbuild.py --release $(PORT_ARCHS)

web:
	@PATH="$(HOME)/emsdk:$(HOME)/emsdk/upstream/emscripten:$$PATH"; $(PY) tools/portbuild.py --web

webcheck: web port
	@EXHUME_EXIT_AFTER=3600000 EXHUME_PORT=$(CURDIR)/build/web-node/uw2port EXHUME_CHEAT_PORT=$(CURDIR)/build/port/uw2port $(PY) tools/replay.py verify all

package:
	@$(PY) tools/package.py $(PACKAGE_ARGS)

test:
	@$(PY) tools/test.py fast

test-full:
	@$(PY) tools/test.py full

verify:
	@$(PY) tools/replay.py verify all

blackbox:
	@UW2DECOMP="$(CURDIR)" $(PY) $(EXHUME)/tools/blackboxcheck.py --config $(EXHUME)/examples/uw2/exhume.toml

golden:
	@$(PY) tools/replay.py golden all

fuzz:
	@$(PY) tools/fuzzasm.py $(FUZZ)

vectors:
	@$(PY) tools/portbuild.py
	@UW2DECOMP="$(CURDIR)" $(PY) $(EXHUME)/tools/vectors.py --config $(EXHUME)/examples/uw2/exhume.toml \
	  --targets tools/vector_targets.py --glue tools/vectorhost-uw2.c --out vectors $(VECTORS)

coverage:
	@$(PY) tools/coverage.py

help:
	@sed -n '1,/^$$/p' Makefile
