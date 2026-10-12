#!/usr/bin/env python3
"""Offline-only bake: NASA linear EXR + HYG 4.1 -> cached GPU sky assets.
Requires numpy, pillow, OpenEXR. No downloads or generation at runtime.
See docs/GALAXY.md for inputs, checksums, coordinates, and attribution.
"""
import argparse
import csv
import hashlib
import json
import math
import mmap
import struct
import tempfile
from pathlib import Path

import numpy as np
import OpenEXR
import Imath
from PIL import Image

OBLIQUITY = math.radians(23.4392911)
SOURCE_HASHES = {
    'catalog': 'd9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd',
    'milky_way': '9d60e58075bcbce9cd03f8115e5635533a0a910007348a485755630a3927b97a',
}


def cie_fit(w):
    """Wyman/Sloan/Shirley 2013 analytic approximation of CIE 1931 2-deg CMFs."""
    def g(center, left, right):
        t = (w - center) * np.where(w < center, left, right)
        return np.exp(-0.5 * t * t)
    return np.stack((1.056*g(599.8,.0264,.0323)+.362*g(442.,.0624,.0374)
                     -.065*g(501.1,.049,.0382),
                     .821*g(568.8,.0213,.0247)+.286*g(530.9,.0613,.0322),
                     1.217*g(437.,.0845,.0278)+.681*g(459.,.0385,.0725)), axis=1)


def stellar_color(bv):
    # Ballesteros B-V -> effective blackbody temperature; clamp outside fit range.
    bv = np.clip(bv, -.4, 2.)
    temperature = 4600*(1/(.92*bv+1.7)+1/(.92*bv+.62))
    wavelength = np.arange(380., 781., 2.)
    spectrum = wavelength**-5 / np.expm1(1.438776877e7/(wavelength*temperature))
    xyz = spectrum @ cie_fit(wavelength)
    rgb = np.array([[3.2406,-1.5372,-.4986],[-.9689,1.8758,.0415],
                    [.0557,-.2040,1.0570]]) @ xyz
    rgb = np.maximum(rgb, 0.)
    return rgb / (rgb @ [.2126,.7152,.0722])


def bake_catalog(path, out):
    records = []
    missing = 0
    with path.open(newline='') as f:
        for row in csv.DictReader(f):
            if row['id'] == '0' or float(row['mag']) > 6.5:
                continue
            ra, dec = float(row['ra'])*math.pi/12, math.radians(float(row['dec']))
            x,y,z = math.cos(dec)*math.cos(ra),math.sin(dec),math.cos(dec)*math.sin(ra)
            direction = (x, y*math.cos(OBLIQUITY)-z*math.sin(OBLIQUITY),
                         y*math.sin(OBLIQUITY)+z*math.cos(OBLIQUITY))
            mag = float(row['mag'])
            if row['ci']:
                color = stellar_color(float(row['ci']))
            else:
                color = np.ones(3)  # Missing photometry stays neutral, never random.
                missing += 1
            records.append((*direction,10**(-.4*mag),*color,mag))
    with (out/'stars.bin').open('wb') as f:
        f.write(b'GGSSTAR1'+struct.pack('<I',len(records)))
        for record in records:
            f.write(struct.pack('<8f',*record))
    return {'stars':len(records),'missing_color_indices':missing,'limiting_magnitude':6.5}


def bake_galaxy(path, out, size):
    # Stream half-float planes through temporary memory maps. The 16k input
    # must not require several full float32 RGB copies on the user's computer.
    with tempfile.TemporaryDirectory(prefix='observatory-sky-') as directory:
        exr = OpenEXR.InputFile(str(path))
        window = exr.header()['dataWindow']
        width = window.max.x-window.min.x+1
        height = window.max.y-window.min.y+1
        planes = []
        for channel in 'RGB':
            target = Path(directory)/channel
            target.write_bytes(exr.channel(channel,Imath.PixelType(Imath.PixelType.HALF)))
            planes.append(np.memmap(target,dtype=np.float16,mode='r',shape=(height,width)))
        exr.close()
        for face,name in enumerate(('px','nx','py','ny','pz','nz')):
            pixels = np.memmap(Path(directory)/'face',dtype=np.uint8,mode='w+',shape=(size,size,3))
            for row in range(0,size,64):
                s,t = np.meshgrid((np.arange(size,dtype=np.float32)+.5)*2/size-1,
                                 (np.arange(row,min(row+64,size),dtype=np.float32)+.5)*2/size-1)
                one = np.ones_like(s)
                directions = ((one,-t,-s),(-one,-t,s),(s,one,t),(s,-one,-t),(s,-t,one),(-s,-t,-one))
                v = np.stack(directions[face],axis=-1)
                v /= np.linalg.norm(v,axis=-1)[...,None]
                x = v[...,0]
                y = v[...,1]*math.cos(OBLIQUITY)+v[...,2]*math.sin(OBLIQUITY)
                z = -v[...,1]*math.sin(OBLIQUITY)+v[...,2]*math.cos(OBLIQUITY)
                u = ((.5-np.arctan2(z,x)/(2*math.pi))%1)*width-.5
                vv = np.clip((.5-np.arcsin(np.clip(y,-1,1))/math.pi)*height-.5,0,height-1)
                xi = np.floor(u).astype(np.int32);yi = np.floor(vv).astype(np.int32)
                fx = u-xi;fy = vv-yi
                linear = np.stack([
                    ((1-fy)*((1-fx)*p[yi,xi%width].astype(np.float32)+fx*p[yi,(xi+1)%width].astype(np.float32))
                     +fy*((1-fx)*p[np.minimum(yi+1,height-1),xi%width].astype(np.float32)
                          +fx*p[np.minimum(yi+1,height-1),(xi+1)%width].astype(np.float32)))
                    for p in planes],axis=-1)
                # NASA stores integrated stellar flux per source texel. A
                # 16k map has sixteen times as many texels as the 4k reference;
                # restore surface brightness before encoding/filtering.
                linear *= (width/4096)**2
                # Retain the source's fine structure; no Gaussian preblur.
                luminance = linear @ np.array([.2126,.7152,.0722])
                linear = .6*linear+.4*luminance[...,None]
                srgb = np.where(linear<=.0031308,12.92*linear,1.055*np.maximum(linear,0)**(1/2.4)-.055)
                pixels[row:row+s.shape[0]] = np.uint8(np.clip(srgb,0,1)*255+.5)
                for p in planes:
                    if hasattr(p._mmap,'madvise'):
                        p._mmap.madvise(mmap.MADV_DONTNEED)
            Image.fromarray(pixels).save(out/f'galaxy-{name}.png')
            del pixels
            print(f'Baked {name}',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=Path,required=True)
    p.add_argument('--milky-way',type=Path,required=True)
    p.add_argument('--output',type=Path,default=Path('assets/sky'))
    p.add_argument('--face-size',type=int,choices=(256,512,1024,2048,4096),default=4096)
    a=p.parse_args()
    for role in SOURCE_HASHES:
        actual=hashlib.sha256(getattr(a,role).read_bytes()).hexdigest()
        if actual!=SOURCE_HASHES[role]:
            p.error(f'{role} checksum does not match the documented source')
    a.output.mkdir(parents=True,exist_ok=True)
    metadata=bake_catalog(a.catalog,a.output)
    bake_galaxy(a.milky_way,a.output,a.face_size)
    metadata.update(face_size=a.face_size,obliquity_degrees=23.4392911,
                    preblur_sigma=0,retained_chroma=.6,source_flux_scale=16,
                    sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in (a.catalog,a.milky_way)})
    (a.output/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata,indent=2))

if __name__=='__main__':main()
