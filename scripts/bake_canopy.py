#!/usr/bin/env python3
"""Bake touching, irregular tree crowns as filtered height/normal/AO fields."""

import argparse
from pathlib import Path
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter


def build_canopy(size=1024, metres=512):
    rng = np.random.default_rng(68192)
    cells = 64
    cell = metres / cells
    h = np.zeros((size, size), dtype=np.float64)
    # Periodic maximum of varied overlapping domes. Dense crowns touch; there
    # is no photograph grain or regularly spaced screen-space dot pattern.
    for z in range(cells):
        for x in range(cells):
            cx = (x + rng.uniform(.2, .8)) * cell
            cz = (z + rng.uniform(.2, .8)) * cell
            rx, rz = rng.uniform(5.5, 8.5, 2)
            height = rng.uniform(9, 16)
            ix = np.arange(int((cx-rx)*size/metres), int((cx+rx)*size/metres)+2)
            iz = np.arange(int((cz-rz)*size/metres), int((cz+rz)*size/metres)+2)
            dx = ((ix+.5)*metres/size-cx)/rx
            dz = ((iz+.5)*metres/size-cz)/rz
            dome = height*np.sqrt(np.clip(1-dz[:,None]**2-dx[None,:]**2, 0, 1))
            yy, xx = np.meshgrid(iz % size, ix % size, indexing='ij')
            h[yy,xx] = np.maximum(h[yy,xx], dome)
    h = gaussian_filter(h, .8, mode='wrap')
    dx = (np.roll(h,-1,axis=1)-np.roll(h,1,axis=1))/(2*metres/size)
    dz = (np.roll(h,-1,axis=0)-np.roll(h,1,axis=0))/(2*metres/size)
    norm = np.sqrt(dx*dx+dz*dz+1)
    nx, nz = -dx/norm, -dz/norm
    ao = .55+.45*np.clip(h/15,0,1)
    fields = np.stack((nx*.5+.5,nz*.5+.5,h/20,ao),axis=-1)
    return np.round(np.clip(fields,0,1)*255).astype('u1')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=Path('assets/world/canopy.png'))
    a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    Image.fromarray(build_canopy()).save(a.output)


if __name__=='__main__':
    main()
