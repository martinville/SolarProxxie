"""Deterministically gzip local web assets; no CDN, filesystem or network needed."""
from pathlib import Path
import argparse,gzip
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]/'web'
a.output.mkdir(parents=True,exist_ok=True)
for name in ('index.html','style.css','app.js','logo.svg'):
    data=(root/name).read_bytes()
    (a.output/(name+'.gz')).write_bytes(gzip.compress(data,compresslevel=9,mtime=0))
