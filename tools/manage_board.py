"""Explicit, identity-checked USB maintenance. Never runs during a build."""
from __future__ import annotations
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
from boards import ROOT, BOARDS, device, rom_port

SEGMENTS=[('0','R2500Adapter.ino.bootloader.bin'),('0x8000','R2500Adapter.ino.partitions.bin'),
          ('0xe000','boot_app0.bin'),('0x10000','R2500Adapter.ino.bin')]

def validate_image(folder,board):
    from image_contract import partition_check, sha
    manifest=json.loads((folder/'manifest.json').read_text())
    contract=json.loads((ROOT/'tests/fixtures/usb-contract.json').read_text())
    if manifest['board']!=board or manifest['descriptor_sha256']!=contract['descriptor_sha256']:
        raise ValueError('Board/USB contract mismatch')
    if manifest.get('maintenance')!='BOOT-R2500!!' or manifest.get('efuse_writes') is not False:
        raise ValueError('Maintenance contract missing')
    required={name for _,name in SEGMENTS}
    if not required.issubset(manifest['files']):raise ValueError('Missing firmware segments')
    for name,expected in manifest['files'].items():
        path=(folder/name).resolve()
        if path.parent!=folder.resolve() or sha(path)!=expected:raise ValueError('Image integrity mismatch: '+name)
    partition_check(folder/'R2500Adapter.ino.partitions.bin')
    app=folder/'R2500Adapter.ino.bin'
    if app.stat().st_size>0x300000:raise ValueError('Application exceeds its partition')
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['identity','status','boot','flash','verify','reset'])
    p.add_argument('--board',choices=BOARDS,required=True);p.add_argument('--config',type=Path)
    p.add_argument('--image-dir',type=Path);p.add_argument('--confirm-write',action='store_true')
    p.add_argument('--rom-only',action='store_true',help='Use ROM commands without uploading the RAM flasher stub')
    args=p.parse_args();identity=device(args.board,args.config)
    if args.action in ('status','boot'):
        import hid
        matches=[d for d in hid.enumerate(0x303a,0x4002) if d.get('serial_number')==identity['serial'] and d.get('usage_page')==0xff00]
        if len(matches)!=1:raise RuntimeError('Expected one registered maintenance HID interface')
        handle=hid.device();handle.open_path(matches[0]['path'])
        try:
            probe=bytes(handle.get_feature_report(1,64))
            if len(probe)!=64 or probe[:7]!=b'\x01UPSDEV':raise RuntimeError('Unexpected maintenance descriptor')
            print(json.dumps({'board':args.board,'protocol_version':chr(probe[7]),'serial':identity['serial']}))
            if args.action=='boot':
                report=bytes([1])+b'BOOT-R2500!!'
                handle.send_feature_report(report.ljust(64,b'\0'))
        finally:handle.close()
        return
    folder=args.image_dir or ROOT/'dist/firmware'/args.board
    if args.action in ('flash','verify'):validate_image(folder,args.board)
    if args.action=='flash':
        if not args.confirm_write:raise RuntimeError('Writing requires --confirm-write')
        if not identity.get('physical_rom_verified'):raise RuntimeError('Verify independent physical BOOT/RESET entry before replacing firmware')
        rollback=identity.get('rollback')
        if not rollback:raise RuntimeError('Register an accessible rollback image and its SHA256 first')
        backup=Path(rollback['path'])
        if hashlib.sha256(backup.read_bytes()).hexdigest()!=rollback['sha256']:raise RuntimeError('Rollback image checksum mismatch')
    port=rom_port(identity)
    command=[sys.executable,'-m','esptool','--chip','esp32s3','--port',port,'--before','no-reset','--after','no-reset']
    if args.rom_only:command.append('--no-stub')
    result=subprocess.check_output(command+['flash-id'],text=True,stderr=subprocess.STDOUT)
    print(result)
    if identity['mac'] not in result.lower() or 'Detected flash size: 16MB' not in result:
        raise RuntimeError('Hardware identity/flash size mismatch; no write performed')
    segments=[value for offset,name in SEGMENTS for value in (offset,str(folder/name))]
    if args.action=='flash':subprocess.run(command+['write-flash','--no-progress',*segments],check=True)
    if args.action in ('flash','verify'):subprocess.run(command+['verify-flash',*segments],check=True)
    if args.action in ('flash','reset'):
        from esptool.cmds import detect_chip
        esp=detect_chip(port=port,connect_mode='no-reset')
        try:
            if esp.CHIP_NAME!='ESP32-S3' or bytes(esp.read_mac()).hex(':')!=identity['mac']:raise RuntimeError('Identity changed')
            esp.write_reg(esp.RTC_CNTL_OPTION1_REG,0,esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
            esp.watchdog_reset()
        finally:esp._port.close()

if __name__=='__main__':main()
