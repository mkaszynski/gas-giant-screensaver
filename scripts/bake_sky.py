#!/usr/bin/env python3
"""Offline-only bake: NASA linear EXR + HYG 4.1 -> cached GPU sky assets.
Requires numpy, scipy, pillow, OpenEXR. No downloads or generation at runtime.
See docs/GALAXY.md for inputs, checksums, coordinates, and attribution.
"""
import argparse
import csv
import hashlib
import json
import math
import struct
from pathlib import Path

import numpy as np
import OpenEXR
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates

OBLIQUITY = math.radians(23.4392911)
SOURCE_HASHES = {
    'catalog': 'd9f69fd86bbf90a4e4d52b4c5c53eacfa6dfc0bfdef85bfd94f095e0bebe4ebd',
    'milky_way': '2eb802d6e68d170b410f766c7fec07f7518619f6b6708fdc81e9302d93e74fdb',
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
    image = OpenEXR.File(str(path)).channels()['RGB'].pixels.astype(np.float32)
    # Average unresolved faint-star speckle at ~0.1 degrees; preserve measured
    # structure and flux in linear light, including dust lanes and star clouds.
    image = gaussian_filter(image, sigma=(1.1,1.1,0), mode=('nearest','wrap','nearest'))
    # Low-light appearance is mostly neutral; preserve gentle real warm/cool hues.
    luminance = image @ np.array([.2126,.7152,.0722],np.float32)
    image = .25*image+.75*luminance[...,None]
    height,width,_ = image.shape
    s,t = np.meshgrid((np.arange(size)+.5)*2/size-1,(np.arange(size)+.5)*2/size-1)
    one = np.ones_like(s)
    faces = [(one,-t,-s),(-one,-t,s),(s,one,t),(s,-one,-t),(s,-t,one),(-s,-t,-one)]
    for name,face in zip(('px','nx','py','ny','pz','nz'),faces):
        v = np.stack(face,axis=-1);v /= np.linalg.norm(v,axis=-1)[...,None]
        # World is ecliptic; inverse obliquity returns J2000 equatorial directions.
        x=v[...,0];y=v[...,1]*math.cos(OBLIQUITY)+v[...,2]*math.sin(OBLIQUITY)
        z=-v[...,1]*math.sin(OBLIQUITY)+v[...,2]*math.cos(OBLIQUITY)
        u=(.5-np.arctan2(z,x)/(2*math.pi))%1
        vv=.5-np.arcsin(np.clip(y,-1,1))/math.pi
        coords=[np.clip(vv*height-.5,0,height-1),u*width-.5]
        # Wrap longitude explicitly; do not wrap the celestial poles.
        padded=np.concatenate((image[:,-1:],image,image[:,:1]),axis=1)
        coords[1]+=1
        linear=np.stack([map_coordinates(padded[...,c],coords,order=1,mode='nearest')
                         for c in range(3)],axis=-1)
        srgb=np.where(linear<=.0031308,12.92*linear,1.055*np.maximum(linear,0)**(1/2.4)-.055)
        Image.fromarray(np.uint8(np.clip(srgb,0,1)*255+.5)).save(out/f'galaxy-{name}.png')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=Path,required=True)
    p.add_argument('--milky-way',type=Path,required=True)
    p.add_argument('--output',type=Path,default=Path('assets/sky'))
    p.add_argument('--face-size',type=int,choices=(256,512,1024,2048),default=1024)
    a=p.parse_args()
    for role in SOURCE_HASHES:
        actual=hashlib.sha256(getattr(a,role).read_bytes()).hexdigest()
        if actual!=SOURCE_HASHES[role]:
            p.error(f'{role} checksum does not match the documented source')
    a.output.mkdir(parents=True,exist_ok=True)
    metadata=bake_catalog(a.catalog,a.output)
    bake_galaxy(a.milky_way,a.output,a.face_size)
    metadata.update(face_size=a.face_size,obliquity_degrees=23.4392911,
                    sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in (a.catalog,a.milky_way)})
    (a.output/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata,indent=2))

if __name__=='__main__':main()
