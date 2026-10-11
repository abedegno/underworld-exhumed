# Underworld Exhumed: both games. Each game folder has its own Makefile (make -C uw2 help);
# this one runs a target in both, or in one with GAME=uw1 or GAME=uw2.
#
#   make setup         Exhume (the submodule) and its Python tools, then each game's setup
#   make setup-toolchain TC_DISKS=DIR TASM_DISKS=DIR   the Borland toolchain both games' DOS
#                      builds use, into uw2/TC and uw2/TASM; needs neither game's EXE
#   make check         each game's gate: every source matches, the exact link is the EXE
#   make test          the gate, the port, quick fuzzing and every session against DOS
#   make port          each game's native port
#   make repocheck     the whole repository: no game data or Borland software, every
#                      script compiles, every Markdown link resolves
GAMES := $(if $(GAME),$(GAME),uw1 uw2)
TARGETS := check test test-full port port-debug port-release package verify vectors \
           setup-port setup-libs setup-sound

.PHONY: $(TARGETS) setup setup-exhume setup-toolchain repocheck help test-coop

# Headless networking tests, no game assets or Borland tools required.
test-coop:
	@python3 tools/test-coop.py --adapter
$(TARGETS):
	@set -e; for g in $(GAMES); do echo "==== $$g: make $@"; $(MAKE) -C $$g $@; done

# the submodule, and what its tools run with: Python (iced-x86, unicorn) for the gate and the
# fuzzing, Node (dos-mcp, js-dos) and emu2 for the DOS the toolchain and the replays run in
setup-exhume:
	@[ -f exhume/tools/gate.py ] || git submodule update --init exhume
	@exhume/.venv/bin/python -c 'import iced_x86, unicorn' 2>/dev/null || { python3 -m venv exhume/.venv && \
	  exhume/.venv/bin/pip install -q -r exhume/tools/requirements.txt; }
	@[ -d exhume/node_modules/dos-mcp ] || npm ci --no-audit --no-fund --prefix exhume
	@sh exhume/tools/setup-emu2.sh || echo "emu2: not built; the toolchain runs in DOSBox-X if installed, else js-dos"
	@echo "Exhume: exhume/ at $$(git -C exhume rev-parse --short HEAD)"

# Turbo C++ 1.01 and TASM 2.0 from your disk images, checked by MD5, where both games' builds look
# (uw2/TC and uw2/TASM; uw1/exhume.toml points there), so that UW1 alone can be set up
setup-toolchain: setup-exhume
	@[ -n "$(TC_DISKS)" ] && [ -n "$(TASM_DISKS)" ] || { echo "make setup-toolchain TC_DISKS=DIR TASM_DISKS=DIR (the folders holding the disk images)"; exit 2; }
	@[ -f uw2/TC/TCC.EXE ] || sh exhume/profiles/borland-tc101/setup-tc.sh "$(TC_DISKS)" uw2/TC
	@[ -f uw2/TASM/TASM.EXE ] || sh exhume/profiles/borland-tc101/setup-tasm.sh "$(TASM_DISKS)" uw2/TASM
	@echo "toolchain: uw2/TC and uw2/TASM"

# a game's own setup where it has one (uw2: the Borland toolchain, DOS, Node), else its port's
setup: setup-exhume
	@set -e; for g in $(GAMES); do t=$$(grep -q '^setup:' $$g/Makefile && echo setup || echo setup-port); \
	  echo "==== $$g: make $$t"; $(MAKE) -C $$g $$t; done

# also: no link into the old repositories' files (they moved into uw1/ and uw2/)
repocheck:
	@[ -f exhume/tools/gate.py ] || git submodule update --init exhume
	@python3 exhume/tools/repocheck.py --root .
	@n=$$(git grep -I -c -E 'github\.com/abedegno/UW[12]Decomp/(blob|tree)/' -- . ':!exhume' | awk -F: '{s+=$$2} END {print s+0}'); \
	  if [ "$$n" -gt 0 ]; then echo "repocheck: $$n links into UW1Decomp or UW2Decomp's files (tools/relink.py)"; exit 1; fi

help:
	@echo "make [GAME=uw1|uw2] setup | $(TARGETS) | repocheck | test-coop"
