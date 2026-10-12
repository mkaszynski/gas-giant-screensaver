#!/usr/bin/env python3
"""Offline BC7 sky compression; requires numpy, pillow, ispc_texcomp==1.0.1.
Use the slow profile and linear-light mipmaps. No encoder runs in the renderer.
"""
import argparse
import importlib.metadata
import json
import struct
from pathlib import Path

import ispc_texcomp as encoder
import numpy as np
from PIL import Image


def next_mip(pixels):
    size = max(1, pixels.shape[0]//2)
    result = np.full((size,size,4),255,dtype=np.uint8)
    for channel in range(3):
        srgb = pixels[...,channel].astype(np.float32)/255
        linear = np.where(srgb<=.04045,srgb/12.92,((srgb+.055)/1.055)**2.4)
        averaged = np.asarray(Image.fromarray(linear).resize((size,size),Image.Resampling.BOX))
        encoded = np.where(averaged<=.0031308,12.92*averaged,1.055*np.maximum(averaged,0)**(1/2.4)-.055)
        result[...,channel] = np.uint8(np.clip(encoded,0,1)*255+.5)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    version = importlib.metadata.version('ispc_texcomp')
    if version!='1.0.1':
        parser.error('Use the documented ispc_texcomp version 1.0.1')
    args.output.mkdir(parents=True,exist_ok=True)
    settings = encoder.BC7EncSettings.from_profile('slow')
    for face in ('px','nx','py','ny','pz','nz'):
        pixels = np.asarray(Image.open(args.input/f'galaxy-{face}.png').convert('RGBA'))
        size = pixels.shape[0]
        if pixels.shape[1]!=size or size<256 or size>4096 or size&(size-1):
            parser.error('Expected square power-of-two faces, 256..4096')
        levels = size.bit_length()
        target = args.output/f'galaxy-{face}.bc7'
        temporary = target.with_suffix('.bc7.tmp')
        with temporary.open('wb') as out:
            out.write(b'GGSCUBE1'+struct.pack('<II',size,levels))
            for level in range(levels):
                dimension = pixels.shape[0]
                padded = np.pad(pixels,((0,(-dimension)%4),(0,(-dimension)%4),(0,0)),mode='edge')
                surface = encoder.RGBASurface(padded.tobytes(),padded.shape[1],padded.shape[0])
                blocks = encoder.compress_blocks_bc7(surface,settings)
                expected = ((dimension+3)//4)**2*16
                if len(blocks)!=expected:
                    raise RuntimeError('Unexpected BC7 output size')
                out.write(struct.pack('<I',len(blocks)))
                out.write(blocks)
                if dimension>1:
                    pixels = next_mip(pixels)
        temporary.replace(target)
        print(f'Compressed {face}: {target.stat().st_size} bytes',flush=True)
    for name in ('stars.bin','manifest.json'):
        source = args.input/name
        if source.resolve()!=(args.output/name).resolve():
            (args.output/name).write_bytes(source.read_bytes())
    manifest = json.loads((args.output/'manifest.json').read_text())
    manifest['gpu_format'] = 'BC7 sRGB, full mip chain'
    manifest['encoder'] = {'name':'ispc_texcomp','version':version,'profile':'slow','mip_filter':'linear-light box'}
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':
    main()
