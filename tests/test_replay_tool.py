import importlib.util,json,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('decoder',ROOT/'tools/packet_decoder/decode.py')
decoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(decoder)
class ReplayTests(unittest.TestCase):
    def test_real_c_parser_on_both_synthetic_layouts(self):
        for layout in (292,302):
            for suffix in ('hex','pcap'):
                output=subprocess.check_output([sys.executable,str(ROOT/'tools/packet_decoder/decode.py'),str(ROOT/f'tests/captures/synthetic{layout}.{suffix}'),'--layout',str(layout),'--json'],text=True)
                j=json.loads(output);self.assertTrue(j['decoded']);self.assertEqual(j['values']['battery_soc']['value'],54);self.assertEqual(j['values']['battery_temperature']['value'],21);self.assertEqual(j['values']['pv_power']['value'],1200)
    def test_supported_linktypes_and_endianness(self):
        raw=(ROOT/'tests/captures/synthetic292.pcap').read_bytes()[40:]
        for endian in ('<','>'):
            for link,prefix in ((101,b''),(1,b'\x00'*12+b'\x08\x00'),(1,b'\x00'*12+b'\x81\x00\x00\x01\x08\x00'),(113,b'\x00'*14+b'\x08\x00'),(276,b'\x08\x00'+b'\x00'*18)):
                packet=prefix+raw
                data=struct.pack(endian+'IHHIIII',0xa1b2c3d4,2,4,0,0,65535,link)+struct.pack(endian+'IIII',1,0,len(packet),len(packet))+packet
                with tempfile.TemporaryDirectory() as tmp:
                    p=Path(tmp)/'capture.pcap';p.write_bytes(data);records=list(decoder.payloads(p,292));self.assertEqual(records,[raw[40:]])
    def test_truncated_pcap_rejected(self):
        data=(ROOT/'tests/captures/synthetic292.pcap').read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'capture.pcap'
            for n in (1,23,30,len(data)-1):
                p.write_bytes(data[:n])
                with self.assertRaises(ValueError):list(decoder.pcap_records(p))
    def test_fragment_is_not_decoded(self):
        p=bytearray((ROOT/'tests/captures/synthetic292.pcap').read_bytes()[40:]);p[6]=0x20
        self.assertIsNone(decoder.tcp_payload(p))
if __name__=='__main__':unittest.main()
