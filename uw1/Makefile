# A decompilation project's build driver, over Exhume's tools/gate.py. Copy it to the
# project's root beside its exhume.toml; the gate's logic and the project's facts (link
# commands, where they write the EXE, boot steps) are in exhume.toml's [gate] section.
#
#   make / make game   modding build, prints the EXE's path
#   make exact         exact link, which must be byte-identical to your original EXE
#   make check         the full gate; recompiles only sources changed since they last passed
#   make check-all     the full gate, recompiling every source
#   make fast          compile, match and verify the changed sources only (not a proof)
#   make boot          boot the modding build and screenshot it
#   make hooks         install the git pre-push hook that runs make check (make test once
#                      there is a port: HOOK="make test")
#
# A native port ([port] and [replay] in exhume.toml, docs/port.md):
#   make setup-exhume  find Exhume ($EXHUME, ../exhume, .exhume, ~/Exhume), or clone it into .exhume (at
#                      the commit tools/exhume-ref names, when there is one)
#   make setup-port    what the port needs to build, per OS, and nothing else: no DOS
#                      toolchain, DOS or game data (Exhume's tools/setup-port.sh)
#   make setup-libs    build SDL3 and libmt32emu from source into tools/libs, where no package
#                      has them (Linux; tools/setup-libs.sh)
#   make setup-sound   fetch the OPL emulator, Nuked OPL3, into tools/nuked-opl3
#   make port-check    compile the game's C for the host, compile only
#   make port          build and link the port; make port-debug the UBSan build
#   make port-release  the build the players' packages are made from (PORT_ARCHS="--arch arm64
#                      --arch x86_64" for a universal macOS build)
#   make web           the WebAssembly build (portbuild.py --web, with emsdk in ~/emsdk and the
#                      libraries SETUP_WEB=1 setup-libs.sh built in tools/libs-web): build/web for
#                      the page, build/web-node/uw1port for Node.js
#   make webcheck      make web, then every session against its golden in the Node.js build
#                      (an hour's --exit-after, EXHUME_EXIT_AFTER: it runs far slower)
#   make package       package the release build for this OS into build/dist (tools/package.py;
#                      PACKAGE_ARGS="--strict --appimage" as CI does)
#   make icons         the icon files from their SVG sources ([package]; tools/icons.py)
#   make test          the gate, the port, the quick fuzzing, every session against its golden
#   make test-full     the long tier: goldens again from DOS, UBSan, drivers, deep fuzzing, coverage
#   make verify        the sessions against their goldens, in the port
#   make blackbox      the black box: a player's sessions recorded, staged and pruned as a
#                      player's build does it (Exhume's tools/blackboxcheck.py, as make test runs it)
#   make golden        every session's golden made again from DOS (twice, checked identical)
#   make golden-check  every session replayed in DOS twice by the replay DOS build and compared
#                      with its committed golden, writing nothing (needs no port)
#   make fuzz          the routine fuzzing (FUZZ=--deep for the long run)
#   make vectors       the test vectors ([vectors]): the original's routines on synthetic inputs,
#                      written to vectors/ and checked against the port's C (VECTORS=--check to
#                      only check that they regenerate byte-identical)
#   make coverage      the port's coverage over the sessions and the fuzzing

# Exhume: $EXHUME, else the submodule ../exhume (a game folder of a repository that holds
# several), else .exhume, else ~/Exhume
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
PY ?= $(if $(wildcard $(EXHUME)/.venv/bin/python),$(EXHUME)/.venv/bin/python,python3)
GATE := $(PY) $(EXHUME)/tools/gate.py --config exhume.toml

HOOK ?= make check
PORT := $(PY) $(EXHUME)/tools

.PHONY: game exact check check-all fast boot hooks repocheck help \
        setup-exhume setup-port setup-libs setup-sound port-check port port-debug port-release web webcheck package icons \
        test test-full verify blackbox golden golden-check fuzz vectors coverage
.DEFAULT_GOAL := game

game:
	@$(GATE) game

exact:
	@$(GATE) exact

check:
	@$(GATE) check

check-all:
	@$(GATE) check --all

fast:
	@$(GATE) check --fast

boot:
	@$(GATE) boot

hooks:
	@sh $(EXHUME)/tools/install-hooks.sh . "$(HOOK)"

setup-exhume:
	@if [ -f ../.gitmodules ] && grep -q 'path = exhume' ../.gitmodules; then \
	  [ -f ../exhume/tools/gate.py ] || git -C .. submodule update --init exhume; echo "Exhume: the submodule ../exhume"; \
	elif [ -f "$(EXHUME)/tools/gate.py" ]; then echo "Exhume: $(EXHUME)"; \
	else git clone -q https://github.com/abedegno/Exhume.git .exhume && \
	  if [ -f tools/exhume-ref ]; then pin=$$(cut -d' ' -f1 tools/exhume-ref); git -C .exhume checkout -q "$$pin"; \
	    echo "Exhume: cloned into .exhume at $$pin (tools/exhume-ref)"; \
	  else echo "Exhume: cloned into .exhume at its default branch (no tools/exhume-ref)"; fi; fi

setup-port: setup-exhume
	@sh $(EXHUME)/tools/setup-port.sh tools/libs tools/nuked-opl3

setup-libs:
	@sh $(EXHUME)/tools/setup-libs.sh tools/libs

setup-sound:
	@sh $(EXHUME)/tools/setup-sound.sh tools/nuked-opl3

port-check:
	@$(PORT)/portcheck.py --config exhume.toml

port:
	@$(PY) tools/portbuild.py --config exhume.toml

port-debug:
	@$(PY) tools/portbuild.py --config exhume.toml --debug

port-release:
	@$(PY) tools/portbuild.py --config exhume.toml --release $(PORT_ARCHS)

web:
	@PATH="$(HOME)/emsdk:$(HOME)/emsdk/upstream/emscripten:$$PATH"; $(PY) tools/portbuild.py --config exhume.toml --web

webcheck: web port
	@EXHUME_EXIT_AFTER=3600000 EXHUME_PORT=$(CURDIR)/build/web-node/uw1port EXHUME_CHEAT_PORT=$(CURDIR)/build/port/uw1port $(PORT)/replay.py --config exhume.toml verify all

package:
	@$(PORT)/package.py --config exhume.toml $(PACKAGE_ARGS)

icons:
	@$(PORT)/icons.py --config exhume.toml

test:
	@$(PORT)/test.py --config exhume.toml fast

test-full:
	@$(PORT)/test.py --config exhume.toml full

verify:
	@$(PORT)/replay.py --config exhume.toml verify all

blackbox:
	@$(PORT)/blackboxcheck.py --config exhume.toml

golden:
	@$(PORT)/replay.py --config exhume.toml golden all

golden-check:
	@$(PORT)/replay.py --config exhume.toml golden all --check

fuzz:
	@$(PORT)/fuzzasm.py --config exhume.toml $(FUZZ)

vectors:
	@$(PORT)/portbuild.py --config exhume.toml
	@$(PORT)/vectors.py --config exhume.toml $(VECTORS)

coverage:
	@$(PORT)/coverage.py --config exhume.toml

repocheck:
	@$(PY) $(EXHUME)/tools/repocheck.py

help:
	@sed -n '1,/^$$/p' Makefile
