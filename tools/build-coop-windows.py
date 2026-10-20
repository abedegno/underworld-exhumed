"""Build a portable Windows Host.exe/Client.exe bundle for both games.

Needs MSYS2 CLANG64's clang, llvm, pkgconf and SDL3, and the usual game sound
dependencies (docs/BUILDING.md). Does not download anything or need game assets.
The launcher uses the .NET Framework compiler shipped with Windows.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build' / 'coop-windows'


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def imports(readobj, binary):
    result = run([readobj, '--coff-imports', binary], capture_output=True, text=True)
    return re.findall(r'^\s+Name: (\S+\.dll)\s*$', result.stdout, re.MULTILINE | re.IGNORECASE)


def copy_runtime(readobj, binary, destination, search):
    """Follow the PE import tables recursively; never depend on the build PATH."""
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination / binary.name)
    system = Path(os.environ['WINDIR']) / 'System32'
    pending, copied = [destination / binary.name], set()
    while pending:
        for name in imports(readobj, pending.pop()):
            if name.lower() in copied:
                continue
            if name.lower().startswith(('api-ms-win-', 'ext-ms-win-')) or (system / name).is_file():
                continue
            source = next((directory / name for directory in search if (directory / name).is_file()), None)
            if source is None:
                raise RuntimeError(f'Missing runtime DLL: {name}')
            if name.lower().startswith('msys-'):
                raise RuntimeError(f'Native game unexpectedly depends on MSYS2: {name}')
            target = destination / name
            shutil.copy2(source, target)
            copied.add(name.lower())
            pending.append(target)
    return sorted(copied)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--msys-root', type=Path, default=ROOT / 'build' / 'toolchain' / 'msys64')
    parser.add_argument('--skip-build', action='store_true', help='package existing port-release binaries')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Run this builder on Windows.')
    msys = args.msys_root.resolve()
    clangbin = msys / 'clang64' / 'bin'
    csc = Path(os.environ['WINDIR']) / 'Microsoft.NET' / 'Framework64' / 'v4.0.30319' / 'csc.exe'
    readobj = clangbin / 'llvm-readobj.exe'
    for required in (clangbin / 'clang.exe', readobj, csc):
        if not required.is_file():
            parser.error(f'Missing build tool: {required}')
    env = dict(os.environ, PATH=str(clangbin) + os.pathsep + os.environ.get('PATH', ''))
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {'mode': 'UW1 independent characters in a host-authoritative shared world; UW2 party co-op', 'games': {}, 'source': run(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                         capture_output=True, text=True).stdout.strip()}
    for game in ('uw1', 'uw2'):
        directory = ROOT / game
        if not args.skip_build:
            command = [sys.executable, 'tools/portbuild.py', '--release']
            if game == 'uw1':
                command += ['--config', 'exhume.toml']
            run(command, cwd=directory, env=env)
        release = directory / 'build' / 'port-release'
        destination = OUT / 'bin' / game
        dlls = copy_runtime(readobj, release / (game + 'port.exe'), destination,
                            [release, directory / 'tools' / 'libs' / 'bin', clangbin])
        if 'nukedopl3.dll' not in dlls:
            raise RuntimeError(f'{game}: FM sound is missing; run setup-sound before building.')
        for name in ('LICENSE', 'NOTICE', 'THIRD-PARTY-NOTICES'):
            shutil.copy2(directory / name, destination / (name + '.txt'))
        readme = (directory / 'tools' / 'dist' / 'README-dist.txt').read_text(encoding='utf-8')
        readme = readme.replace('@VERSION@', 'coop-shared-characters').replace(' @MACOS_OPEN@', '')
        (destination / 'README.txt').write_text(readme, encoding='utf-8')
        # Retain the dependency licence texts alongside each game's replaceable DLLs.
        licenses = ['crt', 'compiler-rt']
        for prefix, name in (('sdl3', 'SDL3'), ('libwinpthread', 'libwinpthread'), ('libc++', 'libc++'),
                             ('libunwind', 'libunwind'), ('libiconv', 'libiconv')):
            if any(dll.startswith(prefix) for dll in dlls):
                licenses.append(name)
        for name in licenses:
            source = msys / 'clang64' / 'share' / 'licenses' / name
            if not source.is_dir():
                raise RuntimeError(f'Missing dependency licence: {source}')
            shutil.copytree(source, destination / 'licenses' / name, dirs_exist_ok=True)
        shutil.copytree(directory / 'tools' / 'nuked-opl3', destination / 'sources' / 'Nuked-OPL3',
                        ignore=shutil.ignore_patterns('.git'), dirs_exist_ok=True)
        munt_docs = directory / 'tools' / 'libs' / 'share' / 'doc' / 'munt' / 'libmt32emu'
        if any(name.startswith('libmt32emu') for name in dlls):
            for name in ('COPYING.txt', 'COPYING.LESSER.txt'):
                target = destination / 'licenses' / 'mt32emu' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(munt_docs / name, target)
        # A clean-PATH launch catches missing imports without any original game files.
        clean_env = dict(env, PATH=os.pathsep.join([str(destination), str(Path(os.environ['WINDIR']) / 'System32'),
                                                   os.environ['WINDIR']]))
        for role in (['--coop-host', '27860'], ['--coop-join', '127.0.0.1:27860']):
            result = run([destination / (game + 'port.exe'), *role, '--help'], env=clean_env,
                         cwd=OUT, capture_output=True, text=True, timeout=20)
            if '--coop-host' not in result.stdout + result.stderr or '--coop-join' not in result.stdout + result.stderr:
                raise RuntimeError(f'{game}: this game binary does not advertise co-op support')
        manifest['games'][game] = {'dlls': dlls, 'clean_path_startup': 'passed'}

    source = ROOT / 'tools' / 'windows' / 'CoopLauncher.cs'
    common = ['/nologo', '/optimize+', '/platform:x64', '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll']
    for role in ('Host', 'Client'):
        command = [csc, *common, '/target:winexe', '/out:' + str(OUT / (role + '.exe')),
                   '/win32icon:' + str(ROOT / 'uw1' / 'tools' / 'dist' / 'icon' / 'uw1.ico')]
        if role == 'Host':
            command.append('/define:HOST')
        run([*command, source], cwd=ROOT)
    test_out = ROOT / 'build' / 'launcher-tests'
    test_out.mkdir(parents=True, exist_ok=True)
    tests = test_out / 'launcher-tests.exe'
    run([csc, *common, '/target:exe', '/main:LauncherTests', '/out:' + str(tests),
         source, ROOT / 'tests' / 'coop' / 'test_launcher.cs'], cwd=ROOT)
    run([tests, OUT], cwd=ROOT)
    shutil.copy2(ROOT / 'tools' / 'windows' / 'README.txt', OUT / 'README.txt')
    shutil.copy2(ROOT / 'LICENSE', OUT / 'LICENSE.txt')
    # Preserve the matching MT-32 sources when built by the local portable toolchain.
    munt_source = ROOT / 'build' / 'toolchain' / 'munt.tar.gz'
    if munt_source.is_file():
        (OUT / 'sources').mkdir(exist_ok=True)
        shutil.copy2(munt_source, OUT / 'sources' / 'munt-libmt32emu_2_8_3.tar.gz')
    for extra_source in sorted((ROOT / 'build' / 'toolchain').glob('*.src.tar.zst')):
        (OUT / 'sources').mkdir(exist_ok=True)
        shutil.copy2(extra_source, OUT / 'sources' / extra_source.name)
    manifest['files'] = {str(path.relative_to(OUT)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in sorted(OUT.rglob('*')) if path.is_file() and path.name != 'build-info.json'}
    (OUT / 'build-info.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    archive = OUT.parent / 'Underworld-Coop-Windows.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(OUT.rglob('*')):
            if path.is_file():
                zipped.write(path, path.relative_to(OUT.parent))
    print(f'Windows co-op package ready: {archive}')


if __name__ == '__main__':
    main()
