'use strict';
// Requested allocation sizes, not driver/physical VRAM measurements.
// The ledger follows actual WebGPU create/destroy calls, including retirement.
(() => {
  function textureBytes(d) {
    const size = d.size;
    const [width, height = 1, depth = 1] = Array.isArray(size)
      ? size
      : [size.width, size.height, size.depthOrArrayLayers];
    const bytes = {
      r8unorm: 1,
      rg8unorm: 2,
      rgba8unorm: 4,
      'rgba8unorm-srgb': 4,
      bgra8unorm: 4,
      r16float: 2,
      rg16float: 4,
      rgba16float: 8,
      rgba32float: 16,
      depth24plus: 4,
      'depth24plus-stencil8': 4,
    }[d.format];
    if (!bytes && !d.format.startsWith('bc6h'))
      throw new Error('Unknown allocation format: ' + d.format);
    let total = 0;
    for (let level = 0; level < (d.mipLevelCount || 1); ++level) {
      const w = Math.max(1, width >> level),
        h = Math.max(1, height >> level);
      const z = d.dimension === '3d' ? Math.max(1, depth >> level) : depth;
      total +=
        (bytes ? w * h * bytes : Math.ceil(w / 4) * Math.ceil(h / 4) * 16) *
        z *
        (d.sampleCount || 1);
    }
    return total;
  }
  const limit = 512 * 1024 * 1024;
  function instrument(device) {
    const state = { bytes: 0, peak: 0, buffers: 0, textures: 0 };
    for (const [method, kind] of [
      ['createBuffer', 'buffers'],
      ['createTexture', 'textures'],
    ]) {
      const create = device[method].bind(device);
      device[method] = (descriptor) => {
        const bytes =
          kind === 'buffers' ? descriptor.size : textureBytes(descriptor);
        if (state.bytes + bytes > limit)
          throw new Error(
            'Graphics resource budget exceeded. Reload and choose Low quality.',
          );
        const resource = create(descriptor),
          destroy = resource.destroy.bind(resource);
        state.bytes += bytes;
        state[kind]++;
        state.peak = Math.max(state.peak, state.bytes);
        let live = true;
        resource.destroy = () => {
          if (live) {
            live = false;
            state.bytes -= bytes;
            state[kind]--;
          }
          destroy();
        };
        return resource;
      };
    }
    return () => ({ ...state });
  }
  function instrumentGL(gl) {
    const state = { bytes: 0, peak: 0, buffers: 0, textures: 0 };
    const textures = new Map(),
      buffers = new Map(),
      renderbuffers = new Map(),
      bound = new Map(),
      units = new Map(),
      elements = new Map();
    let unit = gl.TEXTURE0,
      vao = null,
      renderbuffer = null;
    const wrap = (name, fn) => {
      const original = gl[name].bind(gl);
      gl[name] = (...args) => fn(original, ...args);
    };
    const reserve = (resource, bytes) => {
      const next = state.bytes + bytes - resource.bytes;
      if (next > limit)
        throw new Error(
          'Graphics resource budget exceeded. Reload and choose Low quality.',
        );
      resource.bytes = bytes;
      state.bytes = next;
      state.peak = Math.max(state.peak, next);
    };
    const create = (method, map, kind) =>
      wrap(method, (original, ...args) => {
        const resource = original(...args);
        if (resource) {
          map.set(resource, { bytes: 0, images: new Map() });
          if (kind) state[kind]++;
        }
        return resource;
      });
    const remove = (method, map, kind) =>
      wrap(method, (original, resource) => {
        const data = map.get(resource);
        if (data) {
          reserve(data, 0);
          map.delete(resource);
          if (kind) state[kind]--;
        }
        return original(resource);
      });
    create('createBuffer', buffers, 'buffers');
    remove('deleteBuffer', buffers, 'buffers');
    create('createTexture', textures, 'textures');
    remove('deleteTexture', textures, 'textures');
    create('createRenderbuffer', renderbuffers);
    remove('deleteRenderbuffer', renderbuffers);
    wrap('bindVertexArray', (original, value) => {
      vao = value;
      return original(value);
    });
    wrap('deleteVertexArray', (original, value) => {
      elements.delete(value);
      return original(value);
    });
    wrap('bindBuffer', (original, target, value) => {
      if (target === gl.ELEMENT_ARRAY_BUFFER) elements.set(vao, value);
      else bound.set(target, value);
      return original(target, value);
    });
    wrap('bufferData', (original, target, data, ...args) => {
      const value =
        target === gl.ELEMENT_ARRAY_BUFFER
          ? elements.get(vao)
          : bound.get(target);
      const resource = buffers.get(value);
      const bytes =
        typeof data === 'number'
          ? data
          : (args[2] ?? data.length - (args[1] || 0)) * data.BYTES_PER_ELEMENT;
      if (resource) reserve(resource, bytes);
      return original(target, data, ...args);
    });
    wrap('activeTexture', (original, value) => {
      unit = value;
      return original(value);
    });
    const targetOf = (t) =>
      t >= gl.TEXTURE_CUBE_MAP_POSITIVE_X && t <= gl.TEXTURE_CUBE_MAP_NEGATIVE_Z
        ? gl.TEXTURE_CUBE_MAP
        : t;
    wrap('bindTexture', (original, target, value) => {
      units.set(unit + ':' + target, value);
      return original(target, value);
    });
    const formatOf = (f) =>
      new Map([
        [gl.RGBA8, 'rgba8unorm'],
        [gl.RGB8, 'rgba8unorm'],
        [gl.SRGB8_ALPHA8, 'rgba8unorm-srgb'],
        [gl.RGBA, 'rgba8unorm'],
        [gl.RGB, 'rgba8unorm'],
        [gl.R8, 'r8unorm'],
        [gl.RG8, 'rg8unorm'],
        [gl.R16F, 'r16float'],
        [gl.RG16F, 'rg16float'],
        [gl.RGB16F, 'rgba16float'],
        [gl.RGBA16F, 'rgba16float'],
        [gl.RGBA32F, 'rgba32float'],
        [gl.DEPTH_COMPONENT24, 'depth24plus'],
        [0x8e8f, 'bc6h-rgb-ufloat'],
      ]).get(f);
    const image = (
      target,
      level,
      format,
      width,
      height,
      depth = 1,
      levels = 1,
    ) => {
      const resource = textures.get(units.get(unit + ':' + targetOf(target)));
      if (!resource) return;
      const descriptor = {
        size: [width, height, depth],
        format: formatOf(format),
        dimension: targetOf(target) === gl.TEXTURE_3D ? '3d' : '2d',
        mipLevelCount: levels,
      };
      if (!descriptor.format)
        throw new Error('Unmeasured graphics texture format ' + format);
      const key = target + ':' + level,
        previous = resource.images.get(key);
      const bytes = textureBytes(descriptor);
      reserve(resource, resource.bytes + bytes - (previous?.bytes || 0));
      resource.images.set(key, { descriptor, bytes, level, target });
    };
    wrap('texImage2D', (original, target, level, format, w, h, ...args) => {
      image(target, level, format, w, h);
      return original(target, level, format, w, h, ...args);
    });
    wrap(
      'compressedTexImage2D',
      (original, target, level, format, w, h, ...args) => {
        image(target, level, format, w, h);
        return original(target, level, format, w, h, ...args);
      },
    );
    wrap('texImage3D', (original, target, level, format, w, h, d, ...args) => {
      image(target, level, format, w, h, d);
      return original(target, level, format, w, h, d, ...args);
    });
    wrap('texStorage3D', (original, target, levels, format, w, h, d) => {
      image(target, 0, format, w, h, d, levels);
      return original(target, levels, format, w, h, d);
    });
    wrap('generateMipmap', (original, target) => {
      const resource = textures.get(units.get(unit + ':' + target));
      if (resource)
        for (const entry of [...resource.images.values()])
          if (entry.level === 0) {
            const [w, h, d] = entry.descriptor.size;
            entry.descriptor.mipLevelCount =
              Math.floor(
                Math.log2(
                  Math.max(w, h, entry.descriptor.dimension === '3d' ? d : 1),
                ),
              ) + 1;
            const bytes = textureBytes(entry.descriptor);
            reserve(resource, resource.bytes + bytes - entry.bytes);
            entry.bytes = bytes;
          }
      return original(target);
    });
    wrap('bindRenderbuffer', (original, target, value) => {
      renderbuffer = value;
      return original(target, value);
    });
    const storage = (samples, format, w, h) => {
      const resource = renderbuffers.get(renderbuffer);
      if (resource)
        reserve(
          resource,
          textureBytes({
            size: [w, h],
            format: formatOf(format),
            sampleCount: samples,
          }),
        );
    };
    wrap('renderbufferStorage', (original, target, format, w, h) => {
      storage(1, format, w, h);
      return original(target, format, w, h);
    });
    wrap(
      'renderbufferStorageMultisample',
      (original, target, samples, format, w, h) => {
        storage(samples, format, w, h);
        return original(target, samples, format, w, h);
      },
    );
    gl.memoryStats = () => ({ ...state });
    return gl;
  }
  const api = { textureBytes, instrument, instrumentGL };
  if (typeof module !== 'undefined') module.exports = api;
  else globalThis.ObservatoryGPUMemory = api;
})();
