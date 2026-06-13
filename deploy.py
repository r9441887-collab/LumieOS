"""LumieOS deployment script
Creates bootable USB/eMMC structure or ISO image.

Usage:
  python deploy.py            - creates EFI/BOOT/BOOTX64.EFI
  python deploy.py --iso      - creates bootable ISO (needs xorriso/mkisofs)
"""

import os
import shutil
import struct
import sys

def patch_efi(input_file, output_file):
    with open(input_file, 'rb') as f:
        data = bytearray(f.read())
    
    pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
    subsys_offset = pe_offset + 68
    current = struct.unpack_from('<H', data, subsys_offset)[0]
    
    if current != 10:
        print(f"Patching subsystem: {current} -> 10 (EFI_APPLICATION)")
        struct.pack_into('<H', data, subsys_offset, 10)
    
    with open(output_file, 'wb') as f:
        f.write(data)

def deploy():
    os.makedirs('EFI/BOOT', exist_ok=True)
    if os.path.exists('BOOTX64.EFI'):
        patch_efi('BOOTX64.EFI', 'EFI/BOOT/BOOTX64.EFI')
        print("Deployed: EFI/BOOT/BOOTX64.EFI")
    else:
        print("ERROR: BOOTX64.EFI not found. Build first with build.bat")

def make_iso():
    deploy()
    if shutil.which('xorriso') or shutil.which('mkisofs'):
        maker = 'xorriso' if shutil.which('xorriso') else 'mkisofs'
        cmd = f'{maker} -o lumieos.iso -eltorito-boot EFI/BOOT/BOOTX64.EFI -no-emul-boot .'
        os.system(cmd)
        print("Created: lumieos.iso")
    else:
        print("No ISO maker found. Copy EFI/ directory to FAT32 USB.")

if __name__ == '__main__':
    if '--iso' in sys.argv:
        make_iso()
    else:
        deploy()
