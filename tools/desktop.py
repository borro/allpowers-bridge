"""Native C++/Qt build. Qt and compiler binaries never enter Git."""
from pathlib import Path
import os,shutil,subprocess,sys
from build import ROOT,BUILD,VERSION,environment,run
QT_VERSION='6.8.3'

def qt_root(install=False):
    target=Path(os.environ.get('QT_ROOT') or BUILD/f'qt/{QT_VERSION}/msvc2022_64')
    if not (target/'bin/qmake.exe').exists():
        if not install:raise RuntimeError('Run: uv run --group desktop python tools/desktop.py setup')
        run([sys.executable,'-m','aqt','install-qt','windows','desktop',QT_VERSION,'win64_msvc2022_64',
             '--outputdir',str(BUILD/'qt'),'--archives','qtbase','qttools','--modules','qtconnectivity'])
    config=target/f'include/QtBluetooth/{QT_VERSION}/QtBluetooth/private/qtbluetooth-config_p.h'
    if not config.exists() or '#define QT_FEATURE_winrt_bt 1' not in config.read_text():
        raise RuntimeError('Qt Bluetooth has no WinRT backend. Use the official MSVC build, not MinGW.')
    return target

def msvc_environment(qt):
    installation=os.environ.get('VSINSTALLDIR')
    if not installation:
        locator=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        if locator.exists():installation=subprocess.check_output([str(locator),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    if not installation:raise RuntimeError('Install MSVC 2022 C++ Build Tools with Windows 11 SDK; see docs/building.md')
    vcvars=Path(installation)/'VC/Auxiliary/Build/vcvars64.bat'
    text=subprocess.check_output(f'call "{vcvars}" >nul && set',shell=True,text=True,encoding='utf-8',errors='replace')
    env={k:v for k,v in environment().items() if k.upper()!='PATH'}
    for line in text.splitlines():
        if '=' in line:
            key,value=line.split('=',1)
            if key:env[key]=value
    # Windows environment names are case-insensitive; avoid duplicate Path/PATH.
    path=next((v for k,v in env.items() if k.upper()=='PATH'),'')
    env={k:v for k,v in env.items() if k.upper()!='PATH'}
    env['PATH']=str(qt/'bin')+os.pathsep+str(Path(sys.executable).parent)+os.pathsep+path
    env['VSLANG']='1033'
    return env

def build_desktop():
    qt=qt_root();env=msvc_environment(qt);work=BUILD/'desktop/Windows'
    def execute(args):
        command=list(map(str,args))
        command[0]=shutil.which(command[0],path=env['PATH']) or command[0]
        result=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        work.mkdir(parents=True,exist_ok=True)
        with (work/'build.log').open('ab') as log:log.write(result.stdout)
        output=result.stdout.decode('utf-8',errors='replace').splitlines()
        output=[line for line in output if 'including file:' not in line and 'включение файла:' not in line]
        print('\n'.join(output[-60:] if result.returncode else output[-12:]),flush=True)
        if result.returncode:raise subprocess.CalledProcessError(result.returncode,command)
    execute(['cmake','-S',ROOT/'apps/windows','-B',work,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_CXX_COMPILER=cl.exe',f'-DCMAKE_PREFIX_PATH={qt}',f'-DALLPOWERS_VERSION={VERSION}'])
    execute(['cmake','--build',work,'--parallel','4'])
    execute(['ctest','--test-dir',work,'--output-on-failure'])
    package=ROOT/'dist/windows/AllpowersMonitor'
    if package.exists():shutil.rmtree(package)
    package.mkdir(parents=True);shutil.copy2(work/'AllpowersMonitor.exe',package/'AllpowersMonitor.exe')
    execute([qt/'bin/windeployqt.exe','--release','--no-translations','--no-opengl-sw','--no-compiler-runtime',package/'AllpowersMonitor.exe'])
    redist=next((Path(value) for key,value in env.items() if key.upper()=='VCTOOLSREDISTDIR'),None)
    crt=redist/'x64/Microsoft.VC143.CRT' if redist else None
    if not crt or not crt.is_dir():raise RuntimeError('MSVC redistributable DLLs missing from Build Tools')
    for source in crt.glob('*.dll'):shutil.copy2(source,package/source.name)
    for source in (package/'sqldrivers').glob('*.dll'):
        if source.name.lower()!='qsqlite.dll':source.unlink()
    # Allow the shipped --self-test to run without opening a desktop window.
    shutil.copy2(qt/'plugins/platforms/qoffscreen.dll',package/'platforms/qoffscreen.dll')
    # Native MSVC runtime is deployed by windeployqt. Keep module license texts.
    notices=package/'licenses/Qt';notices.mkdir(parents=True,exist_ok=True)
    for source in (qt/'LICENSES').glob('*'):
        if source.is_file():shutil.copy2(source,notices/source.name)
    if not list(notices.iterdir()):
        import urllib.request
        for name in ('LGPL-3.0-only.txt','GPL-3.0-only.txt','Qt-GPL-exception-1.0.txt'):
            urllib.request.urlretrieve(f'https://raw.githubusercontent.com/qt/qtbase/v{QT_VERSION}/LICENSES/{name}',notices/name)

if __name__=='__main__':
    if sys.argv[1:] == ['setup']:qt_root(install=True)
    else:build_desktop()
