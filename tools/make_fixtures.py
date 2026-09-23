from pathlib import Path
import struct
root=Path(__file__).resolve().parents[1]/'tests/captures'
root.mkdir(parents=True,exist_ok=True)
for size in (292,302):
    p=bytearray(size);p[0]=0xa5;p[11:21]=b'TEST000001';p[37:43]=bytes([24,2,29,12,30,0])
    delta=0 if size==292 else 8
    for offset,value in ((244,54),(240,1210),(176,2333),(248,800),(250,400),(252,0)):
        struct.pack_into('>H',p,offset+delta,value)
    (root/f'synthetic{size}.hex').write_text('# Synthetic protocol fixture; not captured hardware traffic\n'+p.hex(' ')+'\n')
    ip=bytearray(40);ip[0]=0x45;struct.pack_into('>H',ip,2,40+size);ip[8]=64;ip[9]=6;ip[12:16]=bytes([192,168,50,10]);ip[16:20]=bytes([192,0,2,1]);struct.pack_into('>HHI',ip,20,50000,51100,100);ip[32]=0x50;ip[33]=0x18
    # Parser fixture only: no checksum claims and no network transmission.
    raw=ip+p
    (root/f'synthetic{size}.pcap').write_bytes(struct.pack('<IHHIIII',0xa1b2c3d4,2,4,0,0,640,101)+struct.pack('<IIII',1,234000,len(raw),len(raw))+raw)
print('Synthetic fixtures generated')
