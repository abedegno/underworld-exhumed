# The web build

Both games' ports also build to WebAssembly, with Emscripten, and run in a web page. They are the same game and runtime sources as the desktop ports, compiled with `emcc`. The game thread, the PIT thread and the sound run as pthreads, which in a browser are Web Workers on shared memory. The DOS builds and the desktop ports do not change.

Shared memory needs the page to be cross-origin isolated, which needs the COOP and COEP headers. GitHub Pages cannot send them, so the page carries `coi-serviceworker.js`, a service worker that adds them. On a first visit the page reloads itself once while the worker takes over. A browser that has no service workers, or refuses this one (cookies and site data blocked for the site, a private window in some browsers), gets a page that says so in plain words, with its game buttons disabled: the page asks for the worker's registration itself, and a refusal means the reload is never coming. Before that reload has happened the page says nothing; a click in that moment asks the player to try again.

The page is `web/index.html`, `web/page.js`, `web/page.css` and `web/coi-serviceworker.js`. It shows a menu with a button for each game. A game's files and the MT-32 ROMs are downloaded as one packed file per game, kept in the browser's cache in IndexedDB after the first visit. The port's home folder (settings, saved games, recordings) is kept in IndexedDB too. A first visit's home is seeded with `settings-at-start=0` (the game starts at its title, not the settings screen) and `integer=0` (whole-number scaling off), so the picture fills the browser window at 4:3; the player can turn whole-number scaling back on in the settings screen.

## Building

```sh
sh exhume/tools/setup-emsdk.sh            # once: the pinned Emscripten SDK into ~/emsdk
. ~/emsdk/emsdk_env.sh
cd uw1 && SETUP_WEB=1 sh ../exhume/tools/setup-libs.sh tools/libs-web && cd ..   # once per game:
cd uw2 && SETUP_WEB=1 sh ../exhume/tools/setup-libs.sh tools/libs-web && cd ..   # SDL3 and libmt32emu
make -C uw1 web                           # uw1/build/web/uw1port.js and .wasm
make -C uw1 webcheck                      # every recorded session replayed under Node.js
```

`make -C uwN web` builds two programs from the same objects: the page's, in `uwN/build/web`, and one for Node.js, in `uwN/build/web-node`. `make -C uwN webcheck` runs every recorded session in the Node.js one against its DOS golden, as `make test` does for the desktop port. It takes minutes. A setup session made with a test's cheats (`tests/replay/setup/`) is replayed in the desktop build instead (`EXHUME_CHEAT_PORT`), since the web build refuses cheats; the sessions that start from its saved game are replayed in the Node.js one.

The web libraries are built with pthreads on. Anything that links them must also be compiled and linked with `-pthread`, or wasm-ld refuses the link (an object built without threads cannot share memory). `tools/portbuild.py --web` does this for every object.

## Packing a game and checking the page

```sh
SITE=$(mktemp -d)
cp web/*.html web/*.js web/*.css "$SITE"/
mkdir -p "$SITE/uw1" "$SITE/uw2"
cp uw1/build/web/uw1port.* "$SITE/uw1/" && cp uw2/build/web/uw2port.* "$SITE/uw2/"
sh web/pack.sh uw1 ~/UWGOG/UW1 ROMS_DIR "$SITE"
sh web/pack.sh uw2 ~/UWGOG/UW2 ROMS_DIR "$SITE"
node exhume/tools/webcheck.mjs "$SITE"         # the page's checks, in headless Chrome
node exhume/tools/audiocheck-web.mjs "$SITE"   # the sound, in a Chrome with a window
```

`web/pack.sh GAME GAME_DIR ROMS_DIR SITE_DIR` needs the Emscripten SDK in `~/emsdk` (it says so if it is not there) and puts it on its own `PATH`, as the Makefiles' `web` recipe does. It packs one game's files (without its `SAVE` folders, and without a desktop's stray files such as `.DS_Store`, `._*`, `Thumbs.db` and `desktop.ini`) and one MT-32 ROM pair into `SITE_DIR/GAME/GAME.data` and `GAME.data.js`, with Emscripten's file packager. The game is at `/game` in the port's file system and the ROMs at `/game/roms`. It refuses a `SITE_DIR` inside any git repository. `pack.sh` itself is not part of the site.

Only the pair the port would play from is packed, however many images `ROMS_DIR` holds: the control ROM and PCM ROM (or their halves) that the runtime's own choice, `mt32roms_pick` in `exhume/runtime/port/sound/mt32roms.c`, takes from that folder. That choice recognises ROMs by content through libmt32emu, so file names do not matter; it prefers a CM-32L pair, then a CM-32LN pair, then an MT-32 pair, and the newest control ROM. `pack.sh` builds the small `web/romsel.c` with `emcc` against the game's web libmt32emu (`uwN/tools/libs-web`), runs it under Node.js on `ROMS_DIR`, and copies the files it names. A `ROMS_DIR` with no pair is refused.

`tools/webcheck.mjs` serves the site without the COOP and COEP headers, as GitHub Pages does, and checks that the page isolates itself, starts both games (each asking the browser to keep its storage, `navigator.storage.persist`, with the picture filling the window's height, and with only the settings rows that work in a browser on the settings screen, through the port's test hook `web_test_settings`), finds their files and the ROMs, shows the download's progress, opens the settings screen from the gear, says on the menu after its reload that a game has ended, once (`controls`), gives a right click on the picture to the game and not to the browser's context menu, with a left click after it still reaching the game, and has the game let go of a button and a key held when the window loses the focus or the page is hidden (`rightclick`, through the port's test hook `web_test_input`), keeps settings and saves across a reload, explains itself in a browser without service workers or one that refuses the page's worker (`refused`), comes back to the menu with a message when the game stops by a fatal error, a halt or a trap (`stop`, through the port's test hook `web_test_stop`), and gives up a download only when it stalls (`stall`: a slow download that keeps moving runs on, one that stops brings the menu back). `tools/audiocheck-web.mjs` needs a Chrome with a window, since headless Chrome has no audio device; it plays each game for a while and fails if the sound runs dry or nothing reaches the speakers.

## Deploying

The site at https://abedegno.github.io/underworld-exhumed-web/ is deployed as part of a release. Publishing a release (not a prerelease) runs `.github/workflows/site.yml`, which finds each game's latest published final release (the highest `uw1-vX.Y.Z` and `uw2-vX.Y.Z` that is neither a draft nor a prerelease), checks each out at its tag in a worktree of its own, and runs `web/deploy.sh --push --replace` with `--uw1-tree` and `--uw2-tree` naming them, the game files and ROMs from the private bundles, and the deploy key `WEB_DEPLOY_KEY` (docs/BUILDING.md, "Continuous integration"). So the site always runs the latest release of each game, whichever game was released. The page, the page checks and `deploy.sh` itself are the workflow's commit's, which for a release is the published tag's. Run by hand (Actions, `site`, Run workflow) it is a dry run unless `push` is ticked; `skip_replays` passes `--skip-replays`. The run's summary names the release and commit of each game. The workflow file must be in the published tag's commit for the release to run it.

`web/deploy.sh` by hand still works, from any checkout:

```sh
sh web/deploy.sh OWNER/NAME ~/UWGOG/UW1 ~/UWGOG/UW2 ROMS_DIR           # a dry run
sh web/deploy.sh --push OWNER/NAME ~/UWGOG/UW1 ~/UWGOG/UW2 ROMS_DIR    # the deploy
```

The options come before the arguments: an option after `OWNER/NAME` is refused.

`web/deploy.sh` runs `make web webcheck` for both games, assembles the site in a temporary folder as above, packs both games, runs `tools/webcheck.mjs` on it, and checks that no file in it names a local path (the home folder, any `/Users/` path or a temporary folder). Without `--push` it stops there: it lists the files it would push with their sizes, the repository and branch, the commit's identity, and the URL, and contacts nothing. With `--push` it force-pushes the site as a single commit to the `gh-pages` branch of `git@github.com:OWNER/NAME.git`. The commit's author and committer are `Underworld Exhumed site <site@invalid>`, not the deployer's own git identity, since the repository is public and holds the redistributed data; `--own-identity` (with `--push`) makes the commit as the deployer's `user.name` and `user.email` instead. The site is then at `https://OWNER.github.io/NAME/` once the repository's Pages settings serve the `gh-pages` branch.

It refuses a repository that is the origin of this repository or of Exhume, and the project's own repositories by name (underworld-exhumed, Exhume, UW1Decomp, UW2Decomp, uw1-ci-assets, uw2-ci-assets, UWReverseEngineering, UnderworldGodot, emulators, dos-mcp, uw2-personal-notes), before it builds anything. Because the push is forced, `--push` also refuses a repository that already has a `gh-pages` branch unless `--replace` is given too.

`--skip-replays` runs only `make web`, not `make webcheck`, for a quick redeploy of builds that have already been checked. The page checks always run.

`--uw1-tree DIR` and `--uw2-tree DIR` build that game in another checkout of this repository instead of this one, as `site.yml` does with a worktree at each game's release tag (`git worktree add --detach DIR uw1-v1.0.2`, then `git -C DIR submodule update --init`). That tree's own `make web webcheck`, build and `web/pack.sh` are used, so the game, its runtime, its checks and its ROM choice all come from the release; the page, the page checks and the deploy come from the tree `deploy.sh` is in. The site's commit names each game's tree by its release tag (`git describe`).

**The game data and the MT-32 ROMs are the deployer's own.** They go into the site and nowhere else: never into this repository or any other. `*.data` and `*.data.js` are ignored by git here, and both scripts refuse a site folder inside a git repository. The site repository should be one the deployer created for the purpose.

## Playing

- **The settings screen** opens with the gear button at the page's corner, which always works, or with F11 where the browser passes the key on. Cmd+, stays the browser's, and F1 to F10 are the games' own keys (F10 is sleep in both games). Esc closes the screen.
- **Mouse lock:** with the mouse lock option on, a click on the picture locks the pointer through the browser's pointer lock. Esc releases it; that is the browser's rule, and the browser shows its own notice. While the pointer is locked every click goes to the game, so press Esc first to reach the gear, or press F11.
- **Right click:** the game's, as on the desktop: the page keeps the browser's context menu off the game. When the page loses the focus (another window, another tab) the game lets go of any mouse button or key it holds, as the browser may not tell the page about a release while it is away.
- **Full screen:** the button beside the gear. The settings screen has no Fullscreen row on the web: a browser enters full screen only while the page handles a click or a key, and the settings screen's input reaches the port later, from SDL's queue, so the row could not work. Window scale, Game folder and MT-32 ROMs are not offered either: the browser sizes the window, and a browser has no folders to choose (the page gives the port the packed files).
- **Saves and settings** are kept in the browser's IndexedDB for the site. Clearing the site's data loses them. The page asks the browser to keep that storage (`navigator.storage.persist()`, at a game's start, the answer in the console), once in a browser and only when the storage is not persistent already: Firefox asks the player whether to allow it, and asks once, but a browser may still clear it: Safari deletes a site's storage after 7 days of use without a visit unless the site is added to the Home Screen, and other browsers may clear it when the disk is short of space. Saves in a browser are not as safe as saves on the desktop.
- **Loading:** the game's files download once and are then kept by the browser. A slow connection is fine: the page gives up only when nothing has arrived for two minutes, and then reloads itself and says so.
- **When the game stops** (it was quit, or it hit a fatal error, a part the port does not have, or a crash), the page writes the saved files back, returns to the menu with a line saying what happened, and reloads itself for the next game.
