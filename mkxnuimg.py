#!/usr/bin/env python3
"""
Pack the redwood UEFI (with the hook that starts the XNU loader) and an optional payload
into an Android boot image for `fastboot boot`.

  kernel = gzip(BootShim + UEFI FD + ["XNUP" container]) + DTB

The boot shim copies the FD to its base and the payload to PAYLOAD_BASE.

The UEFI pieces are taken from a Mu-Silicium tree built here (Mu-Silicium/, see uefi/README.md)
or, when there is none, from devices/<device>/prebuilt/.

usage: mkxnuimg.py [-p payload.bin] [-o out.img]

Environment (or devices/<device>/<device>.conf, .config CONFIG_DEVICE):
  DEVICE        device name under devices/ (default: from .config, else peridot/redwood)
  UEFI_TARGET   Mu-Silicium device name (default: same as DEVICE)
  UEFI_FD, DTB  files to use instead
  FD_BASE, FD_SIZE, PAYLOAD_BASE, HEADER_VERSION
"""
import argparse, gzip, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MU = os.path.join(HERE, 'Mu-Silicium')

def detect_device():
    d = os.environ.get('DEVICE') or os.environ.get('XL_DEVICE') or ''
    if not d:
        cfg = os.path.join(HERE, '.config')
        if os.path.isfile(cfg):
            m = re.search(r'^CONFIG_DEVICE="([^"]+)"', open(cfg, encoding='utf-8').read(), re.M)
            if m:
                d = m.group(1)
    if not d:
        d = 'peridot' if os.path.isfile(os.path.join(HERE, 'devices', 'peridot', 'peridot.conf')) else 'redwood'
    return d

def load_device_conf(device):
    vals = {}
    conf = os.path.join(HERE, 'devices', device, f'{device}.conf')
    if os.path.isfile(conf):
        for raw in open(conf, encoding='utf-8'):
            line = raw.split('#', 1)[0].strip()
            if '=' in line and not line.startswith(('export ', '.', '[')):
                k, v = line.split('=', 1)
                vals[k.strip()] = v.strip().strip('"').strip("'")
    return vals

DEVICE = detect_device()
CONF = load_device_conf(DEVICE)
def cfg(name, default=''):
    return os.environ.get(name) or CONF.get(name, default)
def env_int(name, default):
    v = cfg(name, '')
    return int(v, 0) if v else default

PREBUILT = os.path.join(HERE, 'devices', DEVICE, 'prebuilt')
TARGET = cfg('UEFI_TARGET', DEVICE)
FD_BASE, FD_SIZE = env_int('FD_BASE', 0x9FD00000), env_int('FD_SIZE', 0x200000)
PAYLOAD_BASE, PAYLOAD_MAX = env_int('PAYLOAD_BASE', 0xF0000000), 0x04000000

ap = argparse.ArgumentParser()
ap.add_argument('-p', '--payload')
ap.add_argument('-o', '--output', default=os.path.join(HERE, f'xnu-{DEVICE}.img'))
ap.add_argument('--mode', default='RELEASE')
args = ap.parse_args()

fd_env = os.environ.get('UEFI_FD') or CONF.get('UEFI_FD', '')
fd_path = fd_env or os.path.join(MU, f'Build/{TARGET}Pkg/{args.mode}_CLANGPDB/FV/SILICIUM_UEFI.fd')
if not os.path.isfile(fd_path) and os.path.isfile(os.path.join(PREBUILT, 'SILICIUM_UEFI.fd')):
    fd_path = os.path.join(PREBUILT, 'SILICIUM_UEFI.fd')
if os.path.isfile(fd_path) and os.path.isdir(os.path.join(MU, 'BootShim')):
    shim_dir = os.path.join(MU, 'BootShim')
    for f in ('BootShim.elf', 'BootShim.bin'):
        try: os.unlink(os.path.join(shim_dir, f))
        except FileNotFoundError: pass
    subprocess.check_call(['make', 'REQUIRES_KERNEL_HEADER=1', f'FD_BASE={FD_BASE:#x}', f'FD_SIZE={FD_SIZE:#x}',
                           f'CFLAGS=--target=aarch64-linux-gnu -DPAYLOAD_BASE={PAYLOAD_BASE:#x}'],
                          cwd=shim_dir, stdout=subprocess.DEVNULL)
    parts = {'shim': os.path.join(shim_dir, 'BootShim.bin'), 'fd': fd_path,
             'dtb': os.path.join(MU, f'Resources/DTBs/{TARGET}.dtb'), 'ramdisk': os.path.join(MU, 'Resources/ramdisk'),
             'mkbootimg': os.path.join(MU, 'Resources/Scripts/mkbootimg.py')}
else:
    dtb_prebuilt = os.path.join(PREBUILT, f'{TARGET}.dtb')
    if not os.path.isfile(dtb_prebuilt):
        dtb_prebuilt = os.path.join(PREBUILT, 'device-tree.dtb')
    parts = {'shim': os.path.join(PREBUILT, 'BootShim.bin'), 'fd': fd_path or os.path.join(PREBUILT, 'SILICIUM_UEFI.fd'),
             'dtb': dtb_prebuilt, 'ramdisk': os.path.join(PREBUILT, 'ramdisk'),
             'mkbootimg': os.path.join(PREBUILT, 'mkbootimg.py')}
dtb_override = cfg('DTB', '')
if dtb_override:
    parts['dtb'] = dtb_override if os.path.isabs(dtb_override) else os.path.join(HERE, dtb_override)
header_version = cfg('HEADER_VERSION', '1')
for name, path in list(parts.items()):
    if name == 'ramdisk' and header_version not in ('0', '1', '2'):
        continue
    if not os.path.isfile(path):
        hint = ''
        if name == 'fd':
            hint = ("\n  The UEFI image is not in the repository: it carries the phone vendor's own drivers, which are not\n"
                    "  redistributed here. Build it from your own phone's firmware: uefi/README.md")
        sys.exit(f'mkxnuimg.py: {path} not found{hint}')
shim = open(parts['shim'], 'rb').read()
fd = open(parts['fd'], 'rb').read()
assert len(fd) == FD_SIZE, hex(len(fd))

blob = shim + fd
if args.payload:
    data = open(args.payload, 'rb').read()
    # the payload file is a complete "XNUP" container (see xnuloader/mkpayload.py)
    assert data[:4] == b'XNUP' and len(data) <= PAYLOAD_MAX - 0x1000, 'bad or oversized payload'
    blob += data
    blob += b'\0' * (-len(blob) % 16)
    print(f'payload: {len(data)} bytes, bytesum={sum(data) & 0xffffffff:08x}')

dtb = open(parts['dtb'], 'rb').read()
kernel = args.output + '.kernel~'
open(kernel, 'wb').write(gzip.compress(blob, 6) + dtb)
try:
    # board "HACKiOS": how the loader of an installed phone tells which A/B slot's boot partition is its own
    # (xnuloader/flashboot.c, ab_switch_back) -- an Android boot image never has that name
    cmd = [sys.executable, parts['mkbootimg'], '--kernel', kernel, '--header_version', header_version, '-o', args.output]
    if header_version in ('0', '1', '2'):
        cmd += ['--ramdisk', parts['ramdisk'], '--board', 'HACKiOS']
    subprocess.check_call(cmd)
finally:
    os.unlink(kernel)
print(f'{args.output}: {os.path.getsize(args.output)} bytes (shim {len(shim)}, uncompressed {len(blob)})')
