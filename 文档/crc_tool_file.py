# -*- coding: utf-8 -*-
import struct

def crc16_modbus(b):
    crc = 0xFFFF
    for x in b:
        crc ^= x
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def frame(dev_id, ftype, cmd, payload=b''):
    body = struct.pack('>HHBHBB', 0xA5B6, dev_id, ftype, cmd, len(payload), 0x02) + payload
    return (body + struct.pack('>HH', crc16_modbus(body), 0xB6A5)).hex().upper()

def name(s):
    b = s.encode('ascii'); return bytes([len(b)]) + b

OFF = 4
def off(v): return v.to_bytes(OFF, 'big')

D = 0x0001
print("ANCHOR reboot:", frame(D, 0x01, 0x0101))
rows = [
 ("T-01 查卡状态/容量",            frame(D,0x01,0x0701)),
 ("T-02 列根目录第0条",            frame(D,0x01,0x0702, bytes([0]))),
 ("T-03 列根目录第1条",            frame(D,0x01,0x0702, bytes([1]))),
 ("T-04 查 DATA.CSV 信息",         frame(D,0x01,0x0706, name("DATA.CSV"))),
 ("T-05 读 DATA.CSV off=0 len=64", frame(D,0x01,0x0703, name("DATA.CSV")+off(0)+bytes([64]))),
 ("T-06 读 DATA.CSV off=64 len=64",frame(D,0x01,0x0703, name("DATA.CSV")+off(64)+bytes([64]))),
 ("T-07 覆盖写 TEST.TXT=Hello",    frame(D,0x01,0x0704, name("TEST.TXT")+bytes([0])+off(0)+b'Hello')),
 ("T-08 追加写 TEST.TXT=World",    frame(D,0x01,0x0704, name("TEST.TXT")+bytes([1])+off(0)+b'World')),
 ("T-09 删除 TEST.TXT",            frame(D,0x01,0x0705, name("TEST.TXT"))),
 ("T-10 开始记录",                 frame(D,0x01,0x0707)),
 ("T-11 停止记录",                 frame(D,0x01,0x0708)),
 ("T-12 按行读 DATA.CSV off=0",    frame(D,0x01,0x0709, name("DATA.CSV")+off(0))),
 ("T-13 读不存在的 NOPE.TXT (期望错误码02)", frame(D,0x01,0x0703, name("NOPE.TXT")+off(0)+bytes([64]))),
 ("T-14 空文件名(名长=0)",          frame(D,0x01,0x0706, bytes([0]))),
 ("T-15 读 TEST.TXT off=0 len=64", frame(D,0x01,0x0703, name("TEST.TXT")+off(0)+bytes([64]))),
 ("T-16 查不存在的 NOPE.TXT 信息 (期望 存在=0)", frame(D,0x01,0x0706, name("NOPE.TXT"))),
]
for k,v in rows: print(f"| {k} | `{v}` |")

# ---- 缓冲区压力帧（验收 P0-1）----
print()
_p17 = name("BIG.TXT") + bytes([0]) + off(0) + b'A' * 200
print(f"| T-17 满片写 BIG.TXT 200字节 (payload={len(_p17)}) | `{frame(D,0x01,0x0704,_p17)}` |")
_p18 = name("STRESS.TXT") + bytes([0]) + off(0) + b'A' * 239
print(f"| T-18 payload={len(_p18)} 超分片上限，应回错误码04 | `{frame(D,0x01,0x0704,_p18)}` |")
