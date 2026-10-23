"""Cross-platform build entry point. Building never opens a device."""
from __future__ import annotations
import argparse, hashlib, json, os, platform, shutil, struct, tomllib
import subprocess, sys, tarfile, urllib.request, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/'build'
CLI_VERSION = '1.5.1'
ESP32_VERSION = '3.3.11'
FQBN = 'esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi'
BOARDS = {'waveshare-147': 1, 'pocket-096': 2}
INDEX = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
VERSION = tomllib.loads((ROOT/'pyproject.toml').read_text(encoding='utf-8'))['project']['version']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def environment():
    env = dict(os.environ, PYTHONUTF8='1')
    for name, directory in [('TEMP','tmp'),('TMP','tmp'),
                            ('ZIG_GLOBAL_CACHE_DIR','zig-global'),('ZIG_LOCAL_CACHE_DIR','zig-local')]:
        folder = BUILD/directory; folder.mkdir(parents=True, exist_ok=True); env[name] = str(folder)
    if sys.platform=='win32':env['TMPDIR']=str(BUILD/'tmp')
    return env

def run(arguments, **kw):
    print('+', ' '.join(map(str, arguments)), flush=True)
    return subprocess.run(list(map(str, arguments)), cwd=kw.pop('cwd',ROOT), env=environment(), check=True, **kw)

def source_hashes():
    files={p.relative_to(ROOT).as_posix():sha(p)
            for base in ('core','firmware','apps','tests','tools','cmake') for p in (ROOT/base).rglob('*')
            if p.is_file() and '__pycache__' not in p.parts and 'data' not in p.parts and 'build' not in p.parts}
    for name in ('CMakeLists.txt','CMakePresets.json','pyproject.toml','uv.lock'):
        files[name]=sha(ROOT/name)
    return files

def inputs(target):
    files=source_hashes()
    if target in BOARDS:
        prefixes=('core/','firmware/','tools/build.py','tests/fixtures/usb-contract.json')
        return {name:sha for name,sha in files.items() if name.startswith(prefixes) or '/' not in name}
    if target=='windows':
        return {name:sha for name,sha in files.items() if name.startswith(('core/','apps/windows/','tests/desktop/','tools/')) or '/' not in name}
    return files

def native():
    compiler = f'{sys.executable};-m;ziglang;c++'
    work=BUILD/'native'/platform.system();work.mkdir(parents=True,exist_ok=True)
    run(['cmake','--preset','dev','-S',ROOT,f'-DPython3_EXECUTABLE={sys.executable}',f'-DCMAKE_CXX_COMPILER={compiler}',
         '-DCMAKE_CXX_FLAGS=-fno-exceptions -fno-rtti'],cwd=work)
    run(['cmake','--build','--preset','dev','--target','core'])

def arduino(install=False):
    settings_file = ROOT/f'.local/arduino.{sys.platform}.json'
    if not settings_file.exists():settings_file=ROOT/'.local/arduino.json'
    settings = json.loads(settings_file.read_text()) if settings_file.exists() else {}
    if settings.get('platform',sys.platform)!=sys.platform:settings={}
    cli = os.environ.get('ARDUINO_CLI') or settings.get('cli') or shutil.which('arduino-cli')
    if not cli:
        folder = BUILD/'tools/arduino-cli'; folder.mkdir(parents=True, exist_ok=True)
        cli = str(folder/('arduino-cli.exe' if sys.platform=='win32' else 'arduino-cli'))
        if not Path(cli).exists():
            if not install: raise RuntimeError('Arduino CLI missing. Run: uv run python tools/build.py setup')
            if platform.machine().lower() not in ('amd64','x86_64'): raise RuntimeError('Automatic CLI setup supports x86-64 only; set ARDUINO_CLI for other hosts')
            suffix = 'Windows_64bit.zip' if sys.platform=='win32' else 'Linux_64bit.tar.gz'
            filename = f'arduino-cli_{CLI_VERSION}_{suffix}'
            base = f'https://github.com/arduino/arduino-cli/releases/download/v{CLI_VERSION}/'
            bundle = folder/filename
            urllib.request.urlretrieve(base+filename, bundle)
            checksums = urllib.request.urlopen(base+f'{CLI_VERSION}-checksums.txt',timeout=60).read().decode()
            expected = next(line.split()[0] for line in checksums.splitlines() if line.split()[-1]==filename)
            if sha(bundle)!=expected: raise RuntimeError('Arduino CLI checksum mismatch')
            if filename.endswith('.zip'):
                with zipfile.ZipFile(bundle) as z: Path(cli).write_bytes(z.read('arduino-cli.exe'))
            else:
                with tarfile.open(bundle) as z: Path(cli).write_bytes(z.extractfile('arduino-cli').read())
                Path(cli).chmod(0o755)
            bundle.unlink()
    version = subprocess.check_output([cli,'version','--format','json'],env=environment(),text=True)
    if json.loads(version)['VersionString'] != CLI_VERSION: raise RuntimeError(f'Use Arduino CLI {CLI_VERSION}')
    host=BUILD/'arduino'/platform.system()
    data = Path(os.environ.get('ARDUINO_DATA_DIR') or settings.get('data') or host/'data')
    config = host/'config.json'; config.parent.mkdir(parents=True,exist_ok=True)
    config.write_text(json.dumps({'directories':{'data':str(data),'downloads':str(host/'downloads'),'user':str(host/'user')},'board_manager':{'additional_urls':[INDEX]}}),encoding='utf-8')
    command = [cli,'--config-file',str(config)]
    if install:
        run(command+['core','update-index'])
        run(command+['core','install',f'esp32:esp32@{ESP32_VERSION}'])
    if not (data/'packages/esp32/hardware/esp32'/ESP32_VERSION).is_dir():
        raise RuntimeError('ESP32 SDK missing. Run setup first.')
    return command, data

def partition_check(path):
    entries={}
    table=path.read_bytes()
    for offset in range(0,len(table),32):
        magic,kind,subtype,address,size,label,flags=struct.unpack_from('<HBBII16sI',table,offset)
        if magic != 0x50aa: break
        entries[label.rstrip(b'\0').decode()]=[address,size]
    contract=json.loads((ROOT/'tests/fixtures/usb-contract.json').read_text())
    for name, expected in contract['partitions'].items():
        if entries.get(name)!=expected: raise RuntimeError(f'Partition changed: {name}')
    return entries

def firmware(board):
    command, data = arduino()
    work=BUILD/'firmware'/board; sketch=work/'sketch/R2500Adapter'
    if sketch.exists(): shutil.rmtree(sketch)
    shutil.copytree(ROOT/'firmware/R2500Adapter',sketch,ignore=shutil.ignore_patterns('build','__pycache__'))
    # Each board owns its sketch and generated sdkconfig; no source-tree writes.
    out=ROOT/'dist/firmware'/board
    if out.exists(): shutil.rmtree(out)
    out.mkdir(parents=True)
    run(command+['compile','--fqbn',FQBN,'--library',str(ROOT/'core'),'--build-path',str(work/'compile'),
                 '--output-dir',str(out),'--build-property',f'compiler.cpp.extra_flags=-DADAPTER_BOARD={BOARDS[board]}',str(sketch)])
    partitions=partition_check(out/'R2500Adapter.ino.partitions.bin')
    boot_app=data/'packages/esp32/hardware/esp32'/ESP32_VERSION/'tools/partitions/boot_app0.bin'
    shutil.copy2(boot_app,out/'boot_app0.bin')
    for p in list(out.iterdir()):
        if p.suffix not in ('.bin',) or '.merged.' in p.name: p.unlink()
    for source in (ROOT/'LICENSE',ROOT/'THIRD_PARTY_NOTICES.md',ROOT/'core/src/AllpowersModels.LICENSE.txt'):
        shutil.copy2(source,out/source.name)
    sdk_root=data/'packages/esp32/hardware/esp32'/ESP32_VERSION
    for name in ('LICENSE','LICENSE.md','LICENSE.txt'):
        if (sdk_root/name).exists():shutil.copy2(sdk_root/name,out/('ESP32-Arduino-'+name))
    files={p.name:sha(p) for p in out.iterdir() if p.is_file()}
    contract=json.loads((ROOT/'tests/fixtures/usb-contract.json').read_text())
    if sha(sketch/'UpsDescriptor.h')!=contract['descriptor_sha256']: raise RuntimeError('USB descriptor changed')
    source=(sketch/'R2500Adapter.ino').read_text()
    if 'BOOT-R2500!!' not in source or 'Console.enableReboot(false)' not in source: raise RuntimeError('Maintenance path missing')
    manifest={'board':board,'project_version':VERSION,'host_platform':platform.system(),
              'firmware_protocol_version':'R','arduino_cli':CLI_VERSION,'esp32_core':ESP32_VERSION,'fqbn':FQBN,
              'files':files,'partitions':partitions,'descriptor_sha256':contract['descriptor_sha256'],
              'maintenance':'BOOT-R2500!!','efuse_writes':False,'sources':inputs(board)}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

def windows(skip_core=False):
    if sys.platform!='win32': raise RuntimeError('Windows packaging must run on Windows; Linux desktop is not implemented')
    from desktop import build_desktop
    build_desktop()
    from package_windows import package_windows
    package_windows()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target',choices=['core',*BOARDS,'windows','all','test','setup','doctor','clean'])
    parser.add_argument('--skip-core',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if args.target=='clean':
        for path in (BUILD,ROOT/'dist'):
            if path.resolve().parent!=ROOT: raise RuntimeError('Unsafe clean path')
            if path.exists():shutil.rmtree(path)
        return
    if args.target=='setup':
        arduino(install=True)
        if sys.platform=='win32':
            from desktop import qt_root
            qt_root(install=True)
        return
    if args.target=='doctor':
        print('Python',sys.version);print('Arduino',arduino()[0])
        if sys.platform=='win32':
            from desktop import qt_root,msvc_environment
            qt=qt_root();env=msvc_environment(qt);print('Qt',qt);print('MSVC',shutil.which('cl.exe',path=env['PATH']))
        return
    before=inputs(args.target)
    if args.target in ('core','test','all'):native()
    if args.target in BOARDS:firmware(args.target)
    if args.target=='windows':windows(args.skip_core)
    if args.target=='all':
        for board in BOARDS:firmware(board)
        if sys.platform=='win32':windows(skip_core=True)
    if args.target=='test':run(['ctest','--preset','dev'])
    if before!=inputs(args.target): raise RuntimeError('Build inputs changed during compilation')

if __name__=='__main__':main()
