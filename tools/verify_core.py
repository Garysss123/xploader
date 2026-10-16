"""Check the pinned native074 source and supplied native core, without hardware claims."""
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
meta=json.loads((root/'releases/native074.json').read_text())
for name,expected in {**meta['source_sha256'],meta['binary']:meta['binary_sha256']}.items():
    if hashlib.sha256((root/name).read_bytes()).hexdigest()!=expected:raise SystemExit('Pinned file differs: '+name)
print('Pinned native074 source and binary verified')
