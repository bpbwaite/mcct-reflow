r"""Exercise the nuvo51icp bridge's passthrough <-> ISP switching without a target chip.

Usage (from tools\): venv\Scripts\python.exe -I bridge_test.py [COM8]
Assumes no target is attached. See AGENTS.md.
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM8'
PACK = 64
CMD_CONNECT, CMD_RUN_APROM, CMD_GET_FWVER = 0xAE, 0xAB, 0xA6


def pkt(cmd, seq=0):
    b = bytearray(PACK)
    b[0:4] = cmd.to_bytes(4, 'little')
    b[4:8] = seq.to_bytes(4, 'little')
    return bytes(b)


def read_for(s, secs):
    end = time.time() + secs
    out = b''
    while time.time() < end:
        out += s.read(s.in_waiting or 1)
    return out


def isp(s, cmd, seq=0, wait=1.5):
    p = pkt(cmd, seq)
    s.reset_input_buffer()
    s.write(p)
    r = read_for(s, wait)
    ok = len(r) >= PACK and int.from_bytes(r[0:2], 'little') == sum(p) & 0xFFFF
    return ok, r


results = []


def check(name, cond, detail=''):
    results.append(cond)
    print(f"{'PASS' if cond else 'FAIL'}  {name}  {detail}")


s = serial.Serial(PORT, 115200, timeout=0.05)   # opening resets the Uno
time.sleep(2.5)                                  # bootloader + setup()
s.reset_input_buffer()

# 1. passthrough: plain text goes to the (absent) target, nothing comes back
s.write(b'hello target\r\n')
r = read_for(s, 0.7)
check('passthrough: text not answered as ISP', len(r) == 0, f'got {len(r)} bytes')

# 2. text containing 0xAE that is not a connect packet must not trigger ISP
s.write(b'x\xae\x00y\xaeZ')
r = read_for(s, 0.7)
check('passthrough: partial signature in data ignored', len(r) == 0, f'got {len(r)} bytes')

# 3. CONNECT switches to ISP and is answered
ok, r = isp(s, CMD_CONNECT)
check('ISP: CONNECT answered', ok, f'{len(r)} bytes')
ok, r = isp(s, CMD_GET_FWVER, 2)
check('ISP: GET_FWVER = 0xE0', ok and r[8] == 0xE0, f'fw=0x{r[8]:02x}' if len(r) > 8 else '')

# 4. RUN_APROM ends the session -> back to passthrough
ok, r = isp(s, CMD_RUN_APROM, 4)
check('ISP: RUN_APROM answered', ok)
s.write(b'back in passthrough?\r\n')
r = read_for(s, 0.7)
check('passthrough again after RUN_APROM', len(r) == 0, f'got {len(r)} bytes')

# 5. a second session works without resetting the Uno
ok, r = isp(s, CMD_CONNECT)
check('ISP: second CONNECT answered', ok)
isp(s, CMD_RUN_APROM, 1)

# 6. false trigger: AE 00 00 00 then silence -> recovers to passthrough
s.write(bytes([CMD_CONNECT, 0, 0, 0]))
time.sleep(0.8)
s.write(b'after false trigger\r\n')
r = read_for(s, 0.7)
check('false trigger: no reply to following text', len(r) == 0, f'got {len(r)} bytes')
ok, r = isp(s, CMD_CONNECT)
check('false trigger: later CONNECT still answered', ok)
isp(s, CMD_RUN_APROM, 1)

s.close()
print(f"\n{sum(results)}/{len(results)} passed")
sys.exit(0 if all(results) else 1)
