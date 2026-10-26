"""构建实际 C51 源码并检查内存；默认内部 RC 为已确认的 22.1184 MHz。"""
from pathlib import Path
import argparse
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / '02 软件源码' / '顺天祥协议转换'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--keil', default='D:/Keil_v5')
    parser.add_argument('--fosc', type=int, default=22118400, help='ISP internal RC frequency in Hz (default: 22118400)')
    args = parser.parse_args()
    tool = Path(args.keil) / 'C51' / 'BIN'
    name = 'Bridge828C'
    out = PROJECT / 'BuildMeasured'
    out.mkdir(exist_ok=True)
    lines = ['Clock mode: internal RC (operator confirmed)', f'FOSC_HZ={args.fosc}', '']
    define = f'PROJECT_FOSC_HZ={args.fosc}UL'
    sources = ['User/main.c', 'App/app_comm.c', 'Control/input_control.c', 'System/System.c', 'System/Uart1.c', 'System/Uart2.c']
    objects = []

    def run(exe, params):
        result = subprocess.run([str(tool / exe), *params], cwd=PROJECT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        msg = result.stdout.decode('gb18030', errors='replace')
        lines.extend(['> ' + exe + ' ' + ' '.join(params), msg])
        print(msg, end='')
        if result.returncode != 0 or re.search(r'\*\*\* (?:ERROR|WARNING|FATAL)', msg):
            raise RuntimeError(f'{exe}: exit={result.returncode}')

    try:
        for src in sources:
            stem = Path(src).stem
            obj = (out / (stem + '.obj')).relative_to(PROJECT).as_posix()
            listing = (out / (stem + '.lst')).relative_to(PROJECT).as_posix()
            run('C51.exe', [src, 'SMALL', 'OPTIMIZE(8,SPEED)', f'DEFINE({define})',
                            'INCDIR(.\\Public;.\\System;.\\App;.\\Control)', f'OBJECT({obj})', f'PRINT({listing})'])
            objects.append(obj)
        startup = (out / 'STARTUP.obj').relative_to(PROJECT).as_posix()
        run('A51.EXE', ['User/STARTUP.A51', f'OBJECT({startup})',
                       f'PRINT({out.name}/STARTUP.lst)'])
        target = f'{out.name}/{name}'
        run('BL51.EXE', [','.join([startup, *objects]), 'TO', target, 'RAMSIZE(256)',
                        'XDATA(0000H-03FFH)', 'CODE(0000H-0EFFFH)',
                        f'PRINT({target}.m51)'])
        mapping = (out / (name + '.m51')).read_text(encoding='gb18030', errors='replace')
        segments = re.findall(r'^\s*IDATA\s+([0-9A-F]+)H\s+([0-9A-F]+)H\s+.*?\?STACK', mapping, re.M)
        if not segments:
            raise RuntimeError('Cannot locate ?STACK in linker map')
        stack_start = int(segments[0][0], 16)
        stack_room = 256 - stack_start
        stack_summary = f'Stack starts at 0x{stack_start:02X}; available IRAM above it: {stack_room} bytes'
        lines.append(stack_summary)
        if stack_room < 64:
            raise RuntimeError(f'Insufficient stack margin: {stack_room} bytes')
        run('OH51.EXE', [target, f'HEXFILE({target}.hex)'])
        lines.append('PASS: C51/A51/BL51/OH51; no warnings; XRAM <= 1024; code <= 60 KiB; stack room >= 64')
        lines.append('Hardware / real 828C application: NOT TESTED')
        print(stack_summary)
        print(lines[-2])
        print(out / (name + '.hex'))
    except Exception as exc:
        lines.append('FAIL: ' + str(exc))
        print(lines[-1], file=sys.stderr)
        return 1
    finally:
        (out / 'build.log').write_text('\n'.join(lines), encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
