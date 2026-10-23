"""Reject private identities, binary build products and machine-specific paths in Git."""
import json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0')
    private=[]
    local=ROOT/'.local/devices.json'
    if local.exists():
        for value in json.loads(local.read_text()).values():private.extend([value.get('mac',''),value.get('serial','')])
    errors=[]
    if not any(names):errors.append('Git index is empty')
    untracked=subprocess.check_output(['git','ls-files','--others','--exclude-standard','-z'],cwd=ROOT).decode().split('\0')
    errors.extend('Untracked public file: '+name for name in untracked if name)
    for name in filter(None,names):
        path=ROOT/name
        if not path.is_file():errors.append(f'Missing tracked file: {name}');continue
        if path.suffix.lower() in ('.bin','.zip','.elf','.dll','.so','.exe','.sqlite3','.log','.pdb','.jsonl'):
            errors.append(f'Generated/private binary: {name}')
        if name.startswith(('.local/','recovery/','artifacts/','evidence/','research/','dist/','build/','.venv/')):
            errors.append(f'Private/output directory: {name}')
        if path.suffix.lower() in ('.png','.ico'):continue
        try:text=subprocess.check_output(['git','show',':'+name],cwd=ROOT).decode('utf-8')
        except UnicodeDecodeError:errors.append(f'Unexpected binary: {name}');continue
        if re.search(r'(?:[A-Z]:|/mnt/[a-z])[/\\]Users[/\\]',text,re.I):errors.append(f'Absolute user path: {name}')
        if any(token and token.lower() in text.lower() for token in private):errors.append(f'Local adapter identity: {name}')
        # A station address must never be baked into a product's connection default.
        if name.endswith('.ino') and re.search(r'"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}"',text,re.I):errors.append(f'Station identity in sketch: {name}')
    if errors:raise SystemExit('\n'.join(errors))
    print(f'PASS: {len(list(filter(None,names)))} tracked files, no private identities or build products')

if __name__=='__main__':main()
