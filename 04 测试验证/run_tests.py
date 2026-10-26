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
               PROJECT / 'System/Uart2.c', PROJECT / 'Control/input_control.c', HERE / 'host_platform.c']
    compiler = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
    cmd = [str(compiler), '/nologo', '/LD', '/TC', '/std:c11', '/utf-8', '/W4', '/WX', '/Od',
           '/DBRIDGE_HOST_TEST', '/D_CRT_SECURE_NO_WARNINGS',
           *['/I' + str(PROJECT / d) for d in ['Public', 'System', 'App', 'Control']],
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
                           ('Error', 0), ('PeakRx', 0), ('OutputOverflow', 0),
                           ('SetInput', 2), ('InputRegister', 1)]:
            fn = getattr(self.lib, 'Host_' + name)
            fn.argtypes = [ctypes.c_uint] * argc
            fn.restype = ctypes.c_uint if name in ['Length', 'Byte', 'State', 'Error', 'PeakRx', 'OutputOverflow', 'InputRegister'] else None
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

    def select_module(self, module):
        for channel in range(1, 5):
            self.lib.Host_SetInput(channel, int(channel == module))

    def start(self, module=1):
        self.select_module(module)
        self.feed(2, f'(START,{module})\r\n')
        self.finish(3000)


def frame(name, value, result='NG', module=1):
    return (json.dumps({'MH': str(module), 'Result': result, name: value},
                       ensure_ascii=False, separators=(',', ':')) + '\r\n').encode('utf-8')


def query_result(module):
    return (json.dumps({'Result': str(module)}, separators=(',', ':')) + '\r\n').encode('utf-8')


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
            values = [tuple(s.strip() for s in part.split('=', 1)) for part in body.split(',') if part.strip()]
            value = (values[0][0] + '=' + values[0][1] + (' ' + unit if unit else '') + ':' + status.upper()) if values else status.upper()
            expected.extend(frame(name, value, status.upper()))
            records.append({'input': line, 'expected': json.loads(frame(name, value, status.upper()))})
        b.feed(1, '\r\n'.join(lines) + '\r\n', spacing)
        b.finish(1_000_000)
        assert b.output(1) == b'test\r\n'
        assert b.output() == expected, (b.output(), expected)
        assert len(records) == 28
        assert records[0]['input'].count('=') == 36
        assert validate_json_stream(b.output())[0]['Open'] == '8-78=978.4 ohm:NG'
        assert b.lib.Host_State() == 0
        (OUT / 'sample_expected.jsonl').write_bytes(expected)
        (OUT / 'sample_mapping.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')

    test('all 28 sample records / first of 36 Open values / continuous 115200', sample_test)
    test('continuous sample with 50 us main-loop scheduling', lambda: sample_test(poll=50))

    for module in range(1, 5):
        def mapping(module=module):
            b.start(module)
            b.feed(1, D1 + SHORT)
            b.finish()
            assert b.output(1) == b'test\r\n'
            assert b.output() == frame('D1-1', '2-5=12.6 V:NG', module=module) + frame('Short', 'OK', 'OK', module)
        test(f'START,{module}; MH mapping; NG then OK independence', mapping)

    def rdnum_ack():
        b.select_module(1)
        b.feed(2, '(RD_NUM)')
        b.finish(5000)
        b.feed(2, 'OK\r\n')
        b.finish(5000)
        assert b.output() == query_result(1)
        assert b.output(1) == b''
    test('RD_NUM without terminator and ignored OK acknowledgement', rdnum_ack)

    def missing_module_fragmented_query():
        b.feed(2, '(RD_')
        b.finish(5000)
        assert b.output() == b'' and b.output(1) == b''
        b.feed(2, 'NUM)\r\n')
        b.finish(5000)
        assert b.output() == ERR and b.output(1) == b''
        assert b.lib.Host_State() == 0
    test('no module: fragmented RD_NUM replies one complete ERR only after closing bracket', missing_module_fragmented_query)

    def missing_module_repeated_queries():
        b.feed(2, '(RD_NUM)(RD_NUM)(RD_NUM)')
        b.finish(20000)
        assert b.output() == ERR * 3 and b.output(1) == b''
        b.finish(4_000_000)
        assert b.output() == ERR * 3 and b.lib.Host_State() == 0
    test('no module: each repeated RD_NUM gets one ERR without ACK or unsolicited repeats', missing_module_repeated_queries)

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
        expected = frame('New\\"Name', 'p1=+0.0100 mV:OK', 'OK', 3)
        expected += frame('Voltage', 'a=0.5:NG', module=3) + frame('Open', 'OK', 'OK', 3)
        assert b.output() == expected, b.output()
    test('generic names / JSON escaping / units / precision / scientific notation / line endings', mixed_fields)

    def utf8_name():
        b.start()
        b.feed(1, '新项目,10,0,V,(a=1.25),OK\r\n')
        b.finish()
        assert b.output() == frame('新项目', 'a=1.25 V:OK', 'OK')
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
        assert b.output() == frame('电阻', 'p=1.00 Ω:OK', 'OK')
    test('UTF-8 unit preserved', utf8_unit)

    def point_escaping():
        b.start(4)
        b.feed(1, 'A,10,0,V,( 测点\\"1 = +1.200 ,other=2,),NG\r\n' + SHORT)
        b.finish()
        assert b.output() == frame('A', '测点\\"1=+1.200 V:NG', module=4) + frame('Short', 'OK', 'OK', 4)
    test('point UTF-8, quote/backslash escaping, trimming and next-frame synchronization', point_escaping)

    def unit_escaping():
        b.start()
        b.feed(1, 'A,1,0,u\\",(p=0.0100,q=0.5),ok\r\n')
        b.finish()
        assert b.output() == frame('A', 'p=0.0100 u\\":OK', 'OK')
    test('unit escaping and first group only on a non-Open project', unit_escaping)

    for invalid in [b'\xc0\x80', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'\xe4\xb8', b'\x80', b'\x00', b'\x7f']:
        def invalid_point(invalid=invalid):
            b.start()
            b.feed(1, b'A,10,0,V,(' + invalid + b'=1,q=2),OK\r\n')
            b.finish()
            assert b.output() == ERR
        test('point encoding/control rejected before any JSON: ' + invalid.hex(), invalid_point)

    def validate_discarded_tail():
        b.start()
        b.feed(1, b'A,10,0,V,(p=1,\xed\xa0\x80=2),OK\r\n')
        b.finish()
        assert b.output() == ERR
    test('discarded groups remain structurally and UTF-8 validated', validate_discarded_tail)

    def fragmented():
        b.lib.Host_SetInput(4, 1)
        b.feed(2, '(STA')
        b.finish(5000)
        assert b.output(1) == b''
        b.feed(2, 'RT,4)')
        b.finish(3000)
        for chunk in [D1[:4], D1[4:19], D1[19:-2], '\r', '\n']:
            b.feed(1, chunk)
            b.finish(1000)
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG', module=4)
    test('partial command and fragmented data never execute early', fragmented)

    def indefinite_first_record():
        b.start()
        b.feed(1, 'test\r\n\r\n')
        b.finish(4_000_000)
        assert b.output() == b''
        b.finish(70_000_000)
        assert b.output() == b'' and b.lib.Host_State() == 2
        b.feed(1, D1)
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG')
    test('no first-record deadline: data after 74 seconds still uploads', indefinite_first_record)

    def no_interframe_timeout():
        b.start()
        b.feed(1, SHORT)
        b.finish(4_000_000)
        b.feed(1, D1)
        b.finish()
        assert b.output() == frame('Short', 'OK', 'OK') + frame('D1-1', '2-5=12.6 V:NG')
    test('no invented inter-frame timeout or acknowledgement dependency', no_interframe_timeout)

    def rollover():
        b.lib.Host_SetMillis(65500)
        b.start()
        b.finish(3_100_000)
        assert b.output() == b''
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == frame('Short', 'OK', 'OK')
    test('old 16-bit millisecond wraparound does not end session', rollover)

    def restart():
        b.start()
        b.select_module(2)
        b.feed(1, D1)
        b.finish(500)
        b.feed(2, '(START,2)\r\n')
        b.finish(15000)
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG')
        assert b.output(1) == b'test\r\ntest\r\n'
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG') + frame('Short', 'OK', 'OK', 2)
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
        assert b.output() == (frame('B', 'p' * (length - len(base) + 1) + '=1 V:OK', 'OK') if length == 511 else ERR)
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
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG') + ERR
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
            value = ('p0=' + values[0] + (' ' + unit if unit else '') + ':' + status) if values else status
            expected.extend(frame(name, value, status, 4))
            b.feed(1, raw)
            b.finish(15000)
        assert b.output() == expected
    test('150 generated records: arbitrary names, units, values, statuses, ring wraparound', generated)

    def input_configuration():
        assert b.lib.Host_InputRegister(0) == 0xFF
        assert b.lib.Host_InputRegister(1) == 0x82
        assert b.lib.Host_InputRegister(2) == 0x81
    test('GPIO init releases X1-X5 and preserves P3.0, P3.1 and P3.7', input_configuration)

    for module in range(1, 5):
        def module_query(module=module):
            b.select_module(module)
            b.feed(2, '(RD_NUM)')
            b.finish(5000)
            assert b.output() == query_result(module)
            assert b.output(1) == b'' and b.lib.Host_State() == 0
        test(f'RD_NUM reads current X{module}, not a software default', module_query)

        def wrong_module(module=module):
            b.select_module(module)
            target = module % 4 + 1
            b.feed(2, f'(START,{target})')
            b.finish(5000)
            assert b.output() == ERR and b.output(1) == b''
            assert b.lib.Host_State() == 0 and b.lib.Host_Error() == 6
            b.select_module(target)
            b.finish(4_000_000)
            assert b.output() == ERR and b.output(1) == b''
            b.feed(2, f'(START,{target})')
            b.finish(3000)
            b.feed(1, SHORT)
            b.finish()
            assert b.output() == ERR + frame('Short', 'OK', 'OK', target)
        test(f'START mismatch with X{module}: immediate one ERR, no delayed start, recovery', wrong_module)

        def x5_module(module=module):
            b.select_module(module)
            b.lib.Host_SetInput(5, 1)
            b.finish(3000)
            assert b.output(1) == bytes.fromhex('74 65 73 74 0d 0a')
            b.feed(1, SHORT + 'end\r\n')
            b.finish(4_000_000)
            assert b.output() == frame('Short', 'OK', 'OK', module)
            assert b.output(1) == b'test\r\n' and b.lib.Host_State() == 0
        test(f'X5 initial low directly starts X{module} MH once; held low after end does not restart', x5_module)

    for active in [(), (1, 2), (1, 2, 3, 4)]:
        def invalid_inputs(active=active):
            for channel in active:
                b.lib.Host_SetInput(channel, 1)
            b.feed(2, '(RD_NUM)')
            b.finish(5000)
            assert b.output() == ERR and b.lib.Host_State() == 0
            b.feed(2, '(START,1)')
            b.finish(5000)
            assert b.output() == ERR * 2 and b.output(1) == b''
            b.lib.Host_SetInput(5, 1)
            b.finish(4_000_000)
            assert b.output() == ERR * 3 and b.output(1) == b''
            # Fixing the module while X5 stays low must not create a new trigger.
            b.select_module(1)
            b.finish(5000)
            assert b.output() == ERR * 3 and b.output(1) == b''
            b.lib.Host_SetInput(5, 0)
            b.finish(100)
            b.lib.Host_SetInput(5, 1)
            b.finish(3000)
            b.feed(1, SHORT)
            b.finish()
            assert b.output() == ERR * 3 + frame('Short', 'OK', 'OK')
        test(f'RD_NUM, START and X5 reject non-unique module inputs {active}; X5 recovery', invalid_inputs)

    def query_during_session():
        b.start(1)
        b.select_module(4)
        b.feed(2, '(RD_NUM)OK')
        b.finish(5000)
        assert b.output() == query_result(4)
        b.select_module(0)
        b.feed(2, '(RD_NUM)')
        b.finish(5000)
        b.lib.Host_SetInput(1, 1)
        b.lib.Host_SetInput(2, 1)
        b.feed(2, '(RD_NUM)')
        b.finish(5000)
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == query_result(4) + ERR * 2 + frame('Short', 'OK', 'OK', 1)
        assert b.output(1) == b'test\r\n'
    test('RD_NUM reports live GPIO; successful/failed query leaves locked MH and session intact', query_during_session)

    def query_during_json():
        b.start(1)
        b.feed(1, D1)
        b.finish(200)
        assert b.output() and not b.output().endswith(b'\r\n')
        b.select_module(3)
        b.feed(2, '(RD_NUM)')
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG') + query_result(3)
        b.feed(1, SHORT)
        b.finish()
        assert b.output().endswith(frame('Short', 'OK', 'OK'))
    test('RD_NUM waits for current JSON boundary and never interleaves bytes', query_during_json)

    def x5_rearm():
        b.select_module(2)
        b.lib.Host_SetInput(5, 1)
        b.finish(3000)
        b.feed(1, SHORT + 'end\r\n')
        b.finish()
        b.lib.Host_SetInput(5, 0)
        b.finish(100)
        b.select_module(4)
        b.lib.Host_SetInput(5, 1)
        b.finish(3000)
        b.feed(1, D1 + 'end\r\n')
        b.finish()
        assert b.output(1) == b'test\r\n' * 2
        assert b.output() == frame('Short', 'OK', 'OK', 2) + frame('D1-1', '2-5=12.6 V:NG', module=4)
    test('X5 high rearms; next low snapshots a new module', x5_rearm)

    for phase in ['sending_test', 'waiting_first', 'sending_json', 'receiving', 'query_reply', 'error_reply']:
        def busy_x5(phase=phase):
            if phase in ['query_reply', 'error_reply']:
                b.select_module(1)
                b.feed(2, '(RD_NUM)' if phase == 'query_reply' else '(BAD)')
                expected = query_result(1) if phase == 'query_reply' else ERR
                assert b.output() and not b.output().endswith(b'\r\n')
                b.lib.Host_SetInput(5, 1)
                b.finish()
                assert b.output() == expected and b.output(1) == b''
                b.finish(5000)
                assert b.output(1) == b''
            else:
                b.select_module(1)
                if phase == 'sending_test':
                    b.feed(2, '(START,1)')
                    assert b.lib.Host_State() == 1
                else:
                    b.start(1)
                expected = b''
                if phase in ['sending_json', 'receiving']:
                    b.feed(1, D1)
                    b.finish(200 if phase == 'sending_json' else 10000)
                    expected = frame('D1-1', '2-5=12.6 V:NG')
                b.lib.Host_SetInput(5, 1)
                b.finish(5000)
                b.feed(1, SHORT + 'end\r\n')
                b.finish()
                assert b.output() == expected + frame('Short', 'OK', 'OK')
                assert b.output(1) == b'test\r\n'
            # A low seen while busy is consumed, never queued into the next idle.
            b.lib.Host_SetInput(5, 0)
            b.finish(100)
            b.lib.Host_SetInput(5, 1)
            b.finish(3000)
            assert b.output(1) == (b'test\r\n' if phase in ['query_reply', 'error_reply'] else b'test\r\n' * 2)
        test('X5 during ' + phase + ': consumed without restart/queued trigger', busy_x5)

    def latch_once():
        b.start(1)
        b.select_module(4)
        b.feed(1, SHORT + SHORT)
        b.finish()
        b.select_module(1)
        b.finish()
        assert b.output(1) == b'test\r\n'
        assert b.output() == frame('Short', 'OK', 'OK') * 2
    test('changing module inputs after start never changes locked MH or retriggers', latch_once)

    def discard_idle_data():
        b.feed(1, 'X' * 900, spacing=0)
        b.finish(10000)
        b.feed(1, D1[:15])
        b.start(2)
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == frame('Short', 'OK', 'OK', 2)
    test('START clears stale idle/overflowed board bytes and partial lines', discard_idle_data)

    def x5_clears_stale():
        b.feed(1, SHORT + D1[:15], spacing=0)
        b.select_module(3)
        b.lib.Host_SetInput(5, 1)
        b.finish(3000)
        b.feed(1, D1)
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG', module=3)
    test('X5 clears pre-trigger board records and fragments', x5_clears_stale)

    def invalid_restart():
        b.start(1)
        b.feed(1, D1)
        b.finish(200)
        b.select_module(2)
        b.feed(2, '(START,3)')
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG') + ERR
        assert b.output(1) == b'test\r\n' and b.lib.Host_State() == 0
        b.feed(1, SHORT)
        b.finish()
        assert b.output() == frame('D1-1', '2-5=12.6 V:NG') + ERR
    test('mismatched new START completes JSON then reports ERR and ends old session', invalid_restart)

    def debug_batch():
        b.start(1)
        b.feed(1, (HERE / '串口测试数据/主板连续输入.txt').read_bytes())
        b.finish()
        # Git checkout may normalize fixture line endings; wire output must be CRLF.
        lines = (HERE / '串口测试数据/上位机预期输出_模号1.jsonl').read_text(encoding='utf-8').splitlines()
        assert b.output() == ''.join(line + '\r\n' for line in lines).encode('utf-8')
        assert b.output(1) == b'test\r\n'
    test('six-record serial debug file matches documented bytes', debug_batch)

    def every_module_combination():
        for mask in range(16):
            b.reset()
            for channel in range(1, 5):
                b.lib.Host_SetInput(channel, mask & (1 << (channel - 1)))
            b.feed(2, '(RD_NUM)')
            b.finish(5000)
            module = {1: 1, 2: 2, 4: 3, 8: 4}.get(mask)
            assert b.output() == (query_result(module) if module else ERR)
            assert b.output(1) == b''
    test('all 16 X1-X4 level combinations: only a unique low selects a module', every_module_combination)

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
