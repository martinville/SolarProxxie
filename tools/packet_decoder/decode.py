"""Decode HEX, raw binary payloads or classic PCAP with the firmware C parser.
Build it first with python tools/test_host.py. PCAPNG must be exported to PCAP.
"""
import argparse,json,struct,subprocess,sys,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def pcap_records(path):
    with path.open('rb') as f:
        header=f.read(24)
        if len(header)!=24:raise ValueError('Truncated PCAP header')
        magic=header[:4]
        if magic in (b'\xd4\xc3\xb2\xa1',b'\x4d\x3c\xb2\xa1'):endian='<'
        elif magic in (b'\xa1\xb2\xc3\xd4',b'\xa1\xb2\x3c\x4d'):endian='>'
        else:raise ValueError('Use classic PCAP; export PCAPNG using Wireshark File → Save As')
        link=struct.unpack_from(endian+'I',header,20)[0]
        if link not in (1,101,113,276):raise ValueError(f'Unsupported link type {link}')
        while h:=f.read(16):
            if len(h)!=16:raise ValueError('Truncated packet header')
            sec,sub,n,wire=struct.unpack(endian+'IIII',h)
            if n>65535 or n>wire:raise ValueError('Invalid packet length')
            p=f.read(n)
            if len(p)!=n:raise ValueError('Truncated packet')
            if link==1:
                if len(p)<14:continue
                ether=struct.unpack_from('>H',p,12)[0];offset=14
                for _ in range(2):
                    if ether not in (0x8100,0x88a8):break
                    if len(p)<offset+4:break
                    ether=struct.unpack_from('>H',p,offset+2)[0];offset+=4
                if ether!=0x0800:continue
                p=p[offset:]
            elif link==113:
                if len(p)<16 or p[14:16]!=b'\x08\x00':continue
                p=p[16:]
            elif link==276:
                if len(p)<20 or p[:2]!=b'\x08\x00':continue
                p=p[20:]
            yield sec,p
def tcp_payload(p):
    if len(p)<40 or p[0]>>4!=4 or p[9]!=6:return None
    ihl=(p[0]&15)*4;total=int.from_bytes(p[2:4],'big')
    if ihl<20 or total>len(p) or total<ihl+20 or int.from_bytes(p[6:8],'big')&0x3fff:return None
    thl=(p[ihl+12]>>4)*4
    if thl<20 or ihl+thl>total:return None
    key=p[12:20]+p[ihl:ihl+4];seq=int.from_bytes(p[ihl+4:ihl+8],'big')+(bool(p[ihl+13]&2))
    return key,seq&0xffffffff,p[ihl+thl:total]
def payloads(path,layout):
    if path.suffix.lower() in ('.hex','.txt'):
        lines=[]
        for line in path.read_text().splitlines():
            line=line.split('#',1)[0]
            line=re.sub(r'^\s*[0-9a-fA-F]{4,8}\s{2,}','',line)
            lines.append(line)
        yield bytes.fromhex(' '.join(lines));return
    if path.suffix.lower() not in ('.pcap','.pcapng'):
        if path.stat().st_size>65535:raise ValueError('Raw sample exceeds 65535 bytes')
        yield path.read_bytes();return
    streams={}
    for timestamp,p in pcap_records(path):
        entry=tcp_payload(p)
        if not entry:continue
        key,seq,data=entry
        if not data:continue
        expected,buf,last=streams.get(key,(seq,b'',timestamp))
        delta=(seq-expected+2**31)%2**32-2**31
        if timestamp-last>30 or delta>0:buf=b''
        elif delta<0:
            if -delta>=len(data):continue
            data=data[-delta:]
        expected=(seq+len(entry[2]))&0xffffffff
        if not buf and data[0]!=0xa5:streams.pop(key,None);continue
        buf+=data
        if len(buf)>2*layout:buf=b''
        while len(buf)>=layout:
            if buf[0]!=0xa5:buf=b'';break
            yield buf[:layout];buf=buf[layout:]
        if len(streams)>=64 and key not in streams:streams.pop(next(iter(streams)))
        streams[key]=(expected,buf,timestamp)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('capture',type=Path);parser.add_argument('--layout',type=int,choices=[292,302,306],default=292);parser.add_argument('--mapping',type=Path,default=ROOT/'mappings'/'sunsynk-inteless.json');parser.add_argument('--modbus-start',type=int);parser.add_argument('--json',action='store_true');args=parser.parse_args()
    exe=ROOT/'build/host'/('packet_decoder.exe' if sys.platform=='win32' else 'packet_decoder')
    if not exe.exists():parser.error('Run python tools/test_host.py to build the shared C decoder')
    if not args.mapping.is_file():parser.error('Mapping file not found')
    command=[str(exe),'--mapping',str(args.mapping)]
    if args.modbus_start is not None:
        if not 0<=args.modbus_start<=65535:parser.error('Invalid Modbus starting register')
        command+=['--modbus-start',str(args.modbus_start)]
    decoded=0
    try:
        for n,p in enumerate(payloads(args.capture,args.layout),1):
            result=json.loads(subprocess.check_output(command,input=p.hex()+'\n',text=True,encoding='utf-8'))
            if args.json:print(json.dumps(result))
            elif result['decoded']:
                print(f'Frame {n} / serial {result["serial"]}')
                for key,value in result['values'].items():print(f'  {key:30s} {value["value"]:12g} {value["unit"]:4s} register {value["register"]}')
            else:print(f'Frame {n}: unknown or malformed',file=sys.stderr)
            decoded+=result['decoded']
    except (ValueError,OSError) as e:parser.error(str(e))
    if not decoded:sys.exit(2)
if __name__=='__main__':main()
