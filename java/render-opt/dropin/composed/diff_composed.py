from pathlib import Path
import numpy as np
from PIL import Image
D=str(Path(__file__).resolve().parent) + "/"
def load(n): return np.asarray(Image.open(D+n).convert("RGB")).astype(np.int16)
off=load("frame_off.png"); a=load("frame_native.png")
mask=off.max(axis=2)>8; ys,xs=np.where(mask); y0,y1,x0,x1=ys.min(),ys.max()+1,xs.min(),xs.max()+1
d=np.abs(a-off)[y0:y1,x0:x1]; per=d.max(axis=2); differ=int((per>0).sum())
print("composed(sin+lightmap+biome) native vs off: max/ch=%d mean=%.3f differ=%d/%d (%.2f%%)"%(int(d.max()),d.mean(),differ,per.size,100.0*differ/per.size))
