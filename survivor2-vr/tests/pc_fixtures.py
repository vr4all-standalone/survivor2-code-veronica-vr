from pathlib import Path
import struct
import sys
base=Path(sys.argv[1]);base.mkdir(parents=True,exist_ok=True)
crcs=[0x756e0de6,0x8073dec7,0x43cd16a4,0xf6aebed8,0xb5e1b582,0x345fd824,0x465ecff1,0x76c92354,0xd45a46ee,0x8c38d1f7,0xf49153c4,0x0e2bdd9a,0x055718ad,0xd06c9bd7,0xdb3c396b]
chips=[(f'chip{i}.bin',0x800000 if i==0 else 0x1000000,c) for i,c in enumerate(crcs)];bios=[('epr-21578h.ic27',0x200000,0x7b452946)]
def zipmeta(path,entries):
 data=bytearray();offs=[]
 for name,size,crc in entries:
  n=name.encode();offs.append(len(data));data+=struct.pack('<I5H3I2H',0x04034b50,20,0,8,0,0,crc,1,size,len(n),0)+n+b'\0'
 start=len(data)
 for (name,size,crc),off in zip(entries,offs):
  n=name.encode();data+=struct.pack('<I6H3I5H2I',0x02014b50,20,20,0,8,0,0,crc,1,size,len(n),0,0,0,0,0,off)+n
 length=len(data)-start;data+=struct.pack('<I4H2IH',0x06054b50,0,0,len(entries),len(entries),length,start,0);path.write_bytes(data)
for name in ['valid','embedded','missing_chip','wrong_chip','bad_duplicate','bad_bios','truncated','missing_bios','missing_zip']:
 d=base/name;d.mkdir(exist_ok=True);cs=chips[:];bs=bios[:]
 if name=='missing_chip':cs=cs[:-1]
 if name=='wrong_chip':cs[0]=(cs[0][0],1,cs[0][2])
 if name=='bad_duplicate':cs=[('bad.bin',1,chips[0][2])]+cs
 if name=='bad_bios':bs=[(bios[0][0],0x200000,123)]
 if name=='embedded':cs+=bios
 if name!='missing_zip':zipmeta(d/'gunsur2.zip',cs)
 if name not in ['missing_bios','embedded']:zipmeta(d/'naomi.zip',bs)
 if name=='truncated':p=d/'gunsur2.zip';p.write_bytes(p.read_bytes()[:-12])
