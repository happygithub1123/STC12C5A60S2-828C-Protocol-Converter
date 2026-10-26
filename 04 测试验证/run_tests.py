"""Compile and exercise the same C parser/encoder/RX rings as the MCU build."""
from pathlib import Path
import ctypes
import json
import os
import random
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PROJECT = ROOT / '02 软件源码' / '顺天祥协议转换'
OUT = HERE / 'results'
OUT.mkdir(exist_ok=True)
LOG = []


def build():
    locator = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    install = subprocess.check_output([str(locator), '-latest', '-products', '*', '-requires',
                                      'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                      '-property', 'installationPath'], encoding='utf-8').strip()
    vcvars = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
    result = subprocess.check_output(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"', encoding='gb18030', errors='replace')
    env = {k.upper(): v for k, v in os.environ.items()}
    for line in result.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k.upper()] = v
    sources = [PROJECT / 'App/app_comm.c', PROJECT / 'System/Uart1.c',
               PROJECT / 'System/Uart2.c', HERE / 'host_platform.c']
    compiler = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
    cmd = [str(compiler), '/nologo', '/LD', '/TC', '/std:c11', '/W4', '/WX', '/Od',
           '/DBRIDGE_HOST_TEST', '/D_CRT_SECURE_NO_WARNINGS',
           *['/I' + str(PROJECT / d) for d in ['Public', 'System', 'App']],
           *map(str, sources), '/link', '/OUT:' + str(OUT / 'bridge_host.dll')]
    result = subprocess.run(cmd, cwd=OUT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    msg = result.stdout.decode('gb18030', errors='replace')
    (OUT / 'host_build.log').write_text(msg, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(msg)


class Bridge:
    def __init__(self):
        self.lib = ctypes.CDLL(str(OUT / 'bridge_host.dll'))
        for name, argc in [('Reset', 0), ('SetPoll', 1), ('Advance', 1), ('SetMillis', 1),
                           ('Receive', 2), ('Length', 1), ('Byte', 2), ('State', 0),
                           ('Error', 0), ('PeakRx', 0), ('OutputOverflow', 0)]:
            fn = getattr(self.lib, 'Host_' + name)
            fn.argtypes = [ctypes.c_uint] * argc
            fn.restype = ctypes.c_uint if name in ['Length', 'Byte', 'State', 'Error', 'PeakRx', 'OutputOverflow'] else None
        self.reset()

    def reset(self):
        self.lib.Host_Reset()

    def advance(self, us):
        self.lib.Host_Advance(us)

    def feed(self, port, text, spacing=87):
        raw = text.encode('utf-8') if isinstance(text, str) else text
        for c in raw:
            self.lib.Host_Receive(port, c)
            if spacing:
                self.advance(spacing)

    def output(self, port=2):
        return bytes(self.lib.Host_Byte(port, i) for i in range(self.lib.Host_Length(port)))

    def finish(self, us=300_000):
        self.advance(us)
        assert not self.lib.Host_OutputOverflow()

    def start(self, module=1):
        self.feed(2, f'(START,{module})\r\n')
        self.finish(3000)


def frame(name, value, result='NG', module=1):
    return (json.dumps({'MH': str(module), 'Result': result, name: value},
                       ensure_ascii=False, separators=(',', ':')) + '\r\n').encode('utf-8')


ERR = b'{"Result":"ERR"}\r\n'
D1 = 'D1-1,9.0,6.0,V,(2-5=12.6),NG\r\n'
SHORT = 'Short,99.9,2.0,Mohm,(),OK\r\n'


def validate_json_stream(raw):
    assert not raw or raw.endswith(b'\r\n'), repr(raw)
    def unique(pairs):
        result = {}
        for k, v in pairs:
            assert k not in result, f'duplicate JSON key: {k}'
            result[k] = v
        return result
    result = []
    for line in raw.split(b'\r\n'):
        if line:
            assert b'\r' not in line and b'\n' not in line
            result.append(json.loads(line, object_pairs_hook=unique))
    return result


def main():
    build()
    b = Bridge()
    passed = 0

    def test(name, run):
        nonlocal passed
        b.reset()
        run()
        validate_json_stream(b.output())
        passed += 1
        LOG.append(f'PASS {name}')
        LOG.append('  UART1_TX_HEX=' + b.output(1).hex(' '))
        LOG.append('  UART2_TX=' + repr(b.output().decode('utf-8')))
        LOG.append(f'  RX_PEAK={b.lib.Host_PeakRx()} LAST_ERROR={b.lib.Host_Error()}')

    def sample_test(spacing=87, poll=10):
        b.lib.Host_SetPoll(poll)
        b.start()
        lines = (ROOT / '01 参考资料/数据新格式.txt').read_text(encoding='utf-8-sig').splitlines()
        expected = bytearray()
        records = []
        for line in lines:
            if line.strip() in ['test', 'end', '']:
                continue
            # Independent host oracle; covers actual values, not just key counts.
            m = re.fullmatch(r'([^,]+),([^,]+),([^,]+),([^,]*),\((.*)\),([^,]+)', line)
            assert m, line
            name, hi, lo, unit, body, status = [s.strip() for s in m.groups()]
            values = [part.split('=', 1)[1].strip() for part in body.split(',') if part.strip()]
            value = ';'.join(v + unit + ':' + status.upper() for v in values) if values else status.upper()
            expected.extend(frame(name, value, status.upper()))
            records.append({'input': line, 'expected': json.loads(frame(name, value, status.upper()))})
        b.feed(1, '\r\n'.join(lines) + '\r\n', spacing)
        b.finish(1_000_000)
        assert b.output(1) == b'test1\r\n'
        assert b.output() == expected, (b.output(), expected)
        assert len(records) == 28
        assert len(validate_json_stream(b.output())[0]['Open'].split(';')) == 36
        assert b.lib.Host_State() == 0
        (OUT / 'sample_expected.jsonl').write_bytes(expected)
        (OUT / 'sample_mapping.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')

    test('all 28 sample records / 36 Open values / continuous 115200', sample_test)
    test('continuous sample with 50 us main-loop scheduling', lambda: sample_test(poll=50))

    for module in range(1, 5):
        def mapping(module=module):
            b.start(module)
            b.feed(1, D1 + SHORT)
            b.finish()
            assert b.output(1) == f'test{module}\r\n'.encode()
            assert b.output() == frame('D1-1', '12.6V:NG', module=module) + frame('Short', 'OK', 'OK', module)
        test(f'START,{module}; MH mapping; NG then OK independence', mapping)

    def rdnum_ack():
        b.feed(2, '(RD_NUM)')
        b.finish(5000)
        b.feed(2, 'OK\r\n')
        b.finish(5000)
        assert b.output() == b'{"Result":"1"}\r\n'
        assert b.output(1) == b''
    test('RD_NUM without terminator and ignored OK acknowledgement', rdnum_ack)

    def bare_ack():
        b.feed(2, 'OK')
        b.finish(5000)
        assert b.output() == b''
        b.start(2)
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == frame('Short', 'OK', 'OK', 2)
    test('bare OK without CRLF does not pollute next START', bare_ack)

    def mixed_fields():
        b.start(3)
        b.feed(1, ' New\\"Name , 99.00 , -3.5 , mV , ( p1=+0.0100 ,p2=-1.20e-3, ),ok\r\n')
        b.feed(1, 'Voltage,10,0,,(a=0.5),NG\n')
        b.feed(1, 'Open,10.0,0.0, ohm,( ),ok\r')
        b.finish()
        expected = frame('New\\"Name', '+0.0100mV:OK;-1.20e-3mV:OK', 'OK', 3)
        expected += frame('Voltage', '0.5:NG', module=3) + frame('Open', 'OK', 'OK', 3)
        assert b.output() == expected, b.output()
    test('generic names / JSON escaping / units / precision / scientific notation / line endings', mixed_fields)

    def utf8_name():
        b.start()
        b.feed(1, '新项目,10,0,V,(a=1.25),OK\r\n')
        b.finish()
        assert b.output() == frame('新项目', '1.25V:OK', 'OK')
    test('UTF-8 project name passthrough', utf8_name)

    for invalid in [b'\xc0\x80', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'\xe4\xb8', b'\x80']:
        def invalid_utf8(invalid=invalid):
            b.start()
            b.feed(1, invalid + b',10,0,V,(),OK\r\n')
            b.finish()
            assert b.output() == ERR
        test('invalid UTF-8 rejected before JSON: ' + invalid.hex(), invalid_utf8)

    def utf8_unit():
        b.start()
        b.feed(1, '电阻,10,0,Ω,(p=1.00),OK\r\n')
        b.finish()
        assert b.output() == frame('电阻', '1.00Ω:OK', 'OK')
    test('UTF-8 unit preserved', utf8_unit)

    def fragmented():
        b.feed(2, '(STA')
        b.finish(5000)
        assert b.output(1) == b''
        b.feed(2, 'RT,4)')
        b.finish(3000)
        for chunk in [D1[:4], D1[4:19], D1[19:-2], '\r', '\n']:
            b.feed(1, chunk)
            b.finish(1000)
        b.finish()
        assert b.output() == frame('D1-1', '12.6V:NG', module=4)
    test('partial command and fragmented data never execute early', fragmented)

    def timeout():
        b.start()
        b.feed(1, 'test\r\n\r\n')
        b.finish(2_900_000)
        assert b.output() == b''
        b.finish(150_000)
        assert b.output() == ERR
        b.finish(4_000_000)
        assert b.output() == ERR
        b.feed(1, D1)
        b.finish()
        assert b.output() == ERR
        b.start(2)
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == ERR + frame('Short', 'OK', 'OK', 2)
    test('3-second first response timeout once / idle data ignored / recovery', timeout)

    def no_interframe_timeout():
        b.start()
        b.feed(1, SHORT)
        b.finish(4_000_000)
        b.feed(1, D1)
        b.finish()
        assert b.output() == frame('Short', 'OK', 'OK') + frame('D1-1', '12.6V:NG')
    test('no invented inter-frame timeout or acknowledgement dependency', no_interframe_timeout)

    def rollover():
        b.lib.Host_SetMillis(65500)
        b.start()
        b.finish(3_100_000)
        assert b.output() == ERR
    test('16-bit millisecond rollover', rollover)

    def restart():
        b.start()
        b.feed(1, D1)
        b.finish(500)
        b.feed(2, '(START,2)\r\n')
        b.finish(15000)
        assert b.output() == frame('D1-1', '12.6V:NG')
        assert b.output(1) == b'test1\r\ntest2\r\n'
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == frame('D1-1', '12.6V:NG') + frame('Short', 'OK', 'OK', 2)
    test('new START waits for complete JSON then replaces session', restart)

    def end():
        b.start()
        b.feed(1, SHORT + 'end\r\n' + D1)
        b.finish()
        assert b.output() == frame('Short', 'OK', 'OK')
        assert b.lib.Host_State() == 0
    test('end closes session without PASS or aggregate JSON', end)

    malformed = [
        'A,1,0,V,(a=1),BAD', 'A,1,0,V,(a=1,broken),OK',
        'A,1,0,V,(a=1,,b=2),OK', 'A,1,0,V,(,a=1),OK',
        'A,1,0,V,(=1),OK', 'A,1,0,V,(a=),OK',
        'A,1,0,V,(a=nan),OK', 'A,1,0,V,(a=1=2),OK',
        'A,1,0,V,(a=1),OK,EXTRA', 'A,1,0,V,(a=1,OK',
        ',1,0,V,(),OK', 'MH,1,0,V,(),OK', 'Result,1,0,V,(),OK',
        'A,xx,0,V,(),OK', 'A,1,0,V,((a=1)),OK',
        'A'*32 + ',1,0,V,(),OK', 'A,1,0,123456789,(),OK',
        'A,1,0,V,(a=1),O\x00', 'end',
    ]
    for i, line in enumerate(malformed):
        def bad(line=line):
            b.start()
            b.feed(1, line + '\r\n')
            b.finish()
            assert b.output() == ERR, (line, b.output())
        test(f'malformed record {i+1}: no partial JSON', bad)

    def boundary(length):
        b.start()
        base = 'B,10,0,V,(p=1),OK'
        raw = base.replace('(p=', '(' + 'p' * (length - len(base) + 1) + '=')
        assert len(raw) == length
        b.feed(1, raw + '\r\n')
        b.finish()
        assert b.output() == (frame('B', '1V:OK', 'OK') if length == 511 else ERR)
    test('511-byte complete input accepted', lambda: boundary(511))
    test('512-byte input rejected', lambda: boundary(512))

    def rx_overflow():
        b.start()
        b.feed(1, 'X' * 900, spacing=0)
        b.finish()
        assert b.output() == ERR
        assert b.lib.Host_Error() == 4
        b.start()
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == ERR + frame('Short', 'OK', 'OK')
    test('UART1 overflow latched and clean recovery', rx_overflow)

    def overflow_during_output():
        b.start()
        b.feed(1, D1)
        b.finish(200)
        b.feed(1, 'X' * 900, spacing=0)
        b.finish()
        assert b.output() == frame('D1-1', '12.6V:NG') + ERR
    test('overflow while sending does not corrupt current JSON', overflow_during_output)

    def rx2_overflow():
        b.feed(2, 'X' * 80, spacing=0)
        b.finish()
        assert b.output() == ERR
        b.start(4)
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == ERR + frame('Short', 'OK', 'OK', 4)
    test('UART2 overflow and command resynchronization', rx2_overflow)

    for command in ['(START,0)', '(START,5)', '(START,12)', '(BAD)', 'X'*40 + '\r\n']:
        def bad_command(command=command):
            b.feed(2, command)
            b.finish()
            assert b.output() == ERR, b.output()
            assert b.output(1) == b''
        test('invalid command ' + repr(command), bad_command)

    def generated():
        rng = random.Random(828)
        b.start(4)
        expected = bytearray()
        for i in range(150):
            name = rng.choice(['Custom', 'R', 'LED', 'PN']) + '-' + str(i)
            unit = rng.choice(['V', 'mV', 'ohm', 'Mohm', 'mA', ''])
            status = rng.choice(['OK', 'NG'])
            values = [f'{rng.randrange(-999,999)}.{rng.randrange(1000):03d}' for _ in range(rng.randrange(5))]
            body = ','.join(f'p{j}={value}' for j, value in enumerate(values))
            raw = f'{name},9999,-9999,{unit},({body}),{status}\r\n'
            value = ';'.join(v + unit + ':' + status for v in values) if values else status
            expected.extend(frame(name, value, status, 4))
            b.feed(1, raw)
            b.finish(15000)
        assert b.output() == expected
    test('150 generated records: arbitrary names, units, values, statuses, ring wraparound', generated)

    LOG.append(f'ALL PASS: {passed} cases. Same C firmware core and RX-ring implementation; simulated UART/time only.')
    LOG.append('NOT VALIDATED: real MCU timing, internal RC drift, physical serial wiring, actual 828C application acceptance.')
    print(LOG[-2])


if __name__ == '__main__':
    try:
        main()
    except Exception:
        import traceback
        LOG.append('FAIL\n' + traceback.format_exc())
        print(LOG[-1], file=sys.stderr)
        sys.exit(1)
    finally:
        (OUT / 'test_results.txt').write_text('\n'.join(LOG) + '\n', encoding='utf-8')
