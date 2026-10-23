"""Retain Qt's official SBOM, copyrights and all referenced license texts."""
import json,re,shutil,urllib.request
from pathlib import Path

def collect_qt_notices(qt:Path,destination:Path,cache:Path):
    destination.mkdir(parents=True,exist_ok=True);cache.mkdir(parents=True,exist_ok=True)
    identifiers=set();copyrights=[]
    for module in ('qtbase','qtconnectivity'):
        source=qt/'sbom'/f'{module}-6.8.3.spdx.json'
        if not source.exists():raise RuntimeError('Use the official Qt 6.8.3 SDK with its attribution SBOM')
        shutil.copy2(source,destination/source.name)
        document=json.loads(source.read_text(encoding='utf-8'))
        for license in document.get('hasExtractedLicensingInfos',[]):
            (destination/(license['licenseId']+'.txt')).write_text(license.get('extractedText',''),encoding='utf-8',newline='\n')
        for package in document['packages']:
            copyrights.append(package['name']+'\n'+package.get('copyrightText','See SDK source notices')+'\n')
            for field in ('licenseDeclared','licenseConcluded'):
                identifiers.update(token for token in re.findall(r'[A-Za-z0-9][A-Za-z0-9.+-]*',package.get(field,''))
                                   if token not in ('AND','OR','WITH','NONE','NOASSERTION') and not token.startswith('LicenseRef-'))
    for identifier in sorted(identifiers):
        target=cache/(identifier+'.txt')
        if not target.exists():
            with urllib.request.urlopen('https://raw.githubusercontent.com/spdx/license-list-data/main/text/'+identifier+'.txt',timeout=30) as stream:
                target.write_bytes(stream.read())
        shutil.copy2(target,destination/target.name)
    (destination/'COPYRIGHTS.txt').write_text('\n'.join(copyrights),encoding='utf-8',newline='\n')
