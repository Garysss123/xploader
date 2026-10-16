"""Convert renderer-produced PPM frames to PNG, preserving their pixels."""
import sys
from pathlib import Path
from PIL import Image
source,destination=map(Path,sys.argv[1:])
destination.mkdir(parents=True,exist_ok=True)
for path in sorted(source.glob('*.ppm')):
    with Image.open(path) as im:im.save(destination/(path.stem+'.png'))
    print(path.stem)
