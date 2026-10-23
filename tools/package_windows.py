"""Package the native Qt executable; never include private runtime data."""
import hashlib,json,shutil,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def package_windows():
    from build import VERSION,source_hashes
    from desktop import qt_root
    from qt_notices import collect_qt_notices
    package=ROOT/'dist/windows/AllpowersMonitor';notices=package/'licenses';notices.mkdir(exist_ok=True)
    collect_qt_notices(qt_root(),notices/'Qt',ROOT/'build/license-cache')
    for source in [ROOT/'LICENSE',ROOT/'THIRD_PARTY_NOTICES.md',ROOT/'core/src/AllpowersModels.LICENSE.txt']:
        shutil.copy2(source,notices/source.name)
    shutil.copy2(ROOT/'THIRD_PARTY_NOTICES.md',package/'THIRD_PARTY_NOTICES.md')
    shutil.copy2(ROOT/'apps/windows/README.md',package/'README.md')
    files={p.relative_to(package).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in package.rglob('*') if p.is_file() and p.name!='manifest.json'}
    if any('data' in Path(name).parts or name.endswith(('.sqlite3','.log','.py','.pyd','.pyc')) for name in files):raise RuntimeError('Private/Python runtime data in native package')
    (package/'manifest.json').write_text(json.dumps({'version':VERSION,'language':'C++17','qt':'6.8.3','platform':'Windows x64','core_abi':1,
        'transport':'Qt Bluetooth / Windows WinRT','os_ups_driver':False,'files':files,'sources':source_hashes()},indent=2)+'\n',encoding='utf-8')
    archive=ROOT/'dist'/f'AllpowersMonitor-{VERSION}-win-x64.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as out:
        for p in package.rglob('*'):
            if p.is_file():out.write(p,'AllpowersMonitor/'+p.relative_to(package).as_posix())
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'archive':str(archive),'sha256':digest}));return archive

if __name__=='__main__':package_windows()
