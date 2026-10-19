'use strict';
// Browser-only command adapter. The existing C++ renderer owns all scene,
// geometry and pass decisions. This implements its bounded GLES subset directly
// with WebGPU resources/commands; it never creates a WebGL rendering context.
(() => {
  const G = WebGL2RenderingContext;
  let nextId = 1;
  const object = (fields) => ({ id: nextId++, ...fields });
  const align = (n, alignment) => Math.ceil(n / alignment) * alignment;
  const {
    sourceKey,
    float16: half,
    writeUniform,
    textureUsage,
  } = ObservatoryGraphicsData;
  const formats = new Map([
    [G.RGBA8, ['rgba8unorm', 4, 1]],
    [G.RGB8, ['rgba8unorm', 4, 1]],
    [G.SRGB8_ALPHA8, ['rgba8unorm-srgb', 4, 1]],
    [G.R8, ['r8unorm', 1, 1]],
    [G.RG8, ['rg8unorm', 2, 1]],
    [G.R16F, ['r16float', 1, 2]],
    [G.RG16F, ['rg16float', 2, 2]],
    [G.RGB16F, ['rgba16float', 4, 2]],
    [G.RGBA16F, ['rgba16float', 4, 2]],
    [G.DEPTH_COMPONENT24, ['depth24plus', 1, 4]],
    [0x8e8f, ['bc6h-rgb-ufloat', 1, 1]],
  ]);
  const blendFactor = (value) =>
    new Map([
      [G.ZERO, 'zero'],
      [G.ONE, 'one'],
      [G.SRC_ALPHA, 'src-alpha'],
      [G.ONE_MINUS_SRC_ALPHA, 'one-minus-src-alpha'],
      [G.DST_ALPHA, 'dst-alpha'],
      [G.ONE_MINUS_DST_ALPHA, 'one-minus-dst-alpha'],
    ]).get(value) ||
    (() => {
      throw new Error(`Unsupported blend factor ${value}`);
    })();

  class ObservatoryGPU {
    constructor(canvas, device, context, shaders) {
      this.canvas = canvas;
      this.device = device;
      this.memoryStats = ObservatoryGPUMemory.instrument(device);
      this.context = context;
      this.shaders = shaders;
      this.format = navigator.gpu.getPreferredCanvasFormat();
      this.width = 0;
      this.height = 0;
      this.textures = Array.from({ length: 32 }, () => new Map());
      this.unit = 0;
      this.buffers = new Map();
      this.enabled = new Set();
      this.defaultVao = object({ attributes: new Map(), element: null });
      this.vao = this.defaultVao;
      this.program = null;
      this.drawFbo = null;
      this.readFbo = null;
      this.renderbuffer = null;
      this.parameters = new Map([
        [G.ACTIVE_TEXTURE, G.TEXTURE0],
        [G.DEPTH_FUNC, G.LESS],
        [G.DEPTH_WRITEMASK, true],
        [G.DEPTH_CLEAR_VALUE, 1],
        [G.COLOR_CLEAR_VALUE, [0, 0, 0, 0]],
        [G.VIEWPORT, [0, 0, canvas.width, canvas.height]],
        [G.SCISSOR_BOX, [0, 0, canvas.width, canvas.height]],
        [G.BLEND_SRC_RGB, G.ONE],
        [G.BLEND_DST_RGB, G.ZERO],
        [G.BLEND_SRC_ALPHA, G.ONE],
        [G.BLEND_DST_ALPHA, G.ZERO],
        [G.BLEND_EQUATION_RGB, G.FUNC_ADD],
        [G.BLEND_EQUATION_ALPHA, G.FUNC_ADD],
        [G.POLYGON_OFFSET_FACTOR, 0],
        [G.POLYGON_OFFSET_UNITS, 0],
        [G.UNPACK_ALIGNMENT, 4],
      ]);
      this.encoder = null;
      this.pass = null;
      this.passFbo = null;
      this.retired = [];
      this.pipelineCache = new Map();
      this.samplerCache = new Map();
      this.uniformBytes = new ArrayBuffer(4 * 1024 * 1024);
      this.uniformBuffer = device.createBuffer({
        size: this.uniformBytes.byteLength,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      });
      this.uniformCursor = 0;
      this.extensions = [
        'EXT_color_buffer_float',
        'OES_texture_float_linear',
        'EXT_float_blend',
        'EXT_texture_filter_anisotropic',
      ];
      if (device.features.has('texture-compression-bc'))
        this.extensions.push('EXT_texture_compression_bptc');
      this.configure();
    }
    configure() {
      if (
        this.width === this.canvas.width &&
        this.height === this.canvas.height
      )
        return;
      this.endPass();
      this.context.configure({
        device: this.device,
        format: this.format,
        alphaMode: 'opaque',
      });
      this.width = this.canvas.width;
      this.height = this.canvas.height;
      this.swapTexture = null;
    }
    commands() {
      return (this.encoder ||= this.device.createCommandEncoder());
    }
    endPass() {
      if (!this.pass) return;
      this.pass.end();
      this.pass = null;
    }
    submit() {
      this.endPass();
      if (!this.encoder) {
        const retired = this.retired.splice(0);
        return this.device.queue
          .onSubmittedWorkDone()
          .then(() => retired.forEach((resource) => resource.destroy()));
      }
      if (this.uniformCursor)
        this.device.queue.writeBuffer(
          this.uniformBuffer,
          0,
          this.uniformBytes,
          0,
          this.uniformCursor,
        );
      this.device.queue.submit([this.encoder.finish()]);
      this.encoder = null;
      // writeBuffer captures the CPU bytes and is ordered after the preceding
      // submission. The next frame can encode into the same bounded arena now.
      this.uniformCursor = 0;
      const retired = this.retired.splice(0);
      this.swapTexture = null;
      return this.device.queue.onSubmittedWorkDone().then(() => {
        retired.forEach((resource) => resource.destroy());
      });
    }
    getSupportedExtensions() {
      return this.extensions;
    }
    getExtension(name) {
      if (name === 'EXT_texture_filter_anisotropic')
        return {
          TEXTURE_MAX_ANISOTROPY_EXT: 0x84fe,
          MAX_TEXTURE_MAX_ANISOTROPY_EXT: 0x84ff,
        };
      return this.extensions.includes(name) ? {} : null;
    }
    getContextAttributes() {
      return { alpha: false, depth: false, antialias: false };
    }
    isContextLost() {
      return false;
    }
    getError() {
      return G.NO_ERROR;
    }
    getParameter(name) {
      if (this.parameters.has(name)) return this.parameters.get(name);
      switch (name) {
        case G.VERSION:
          return 'WebGL 2.0 (Observatory WebGPU)';
        case G.SHADING_LANGUAGE_VERSION:
          return 'WebGL GLSL ES 3.00 (offline WGSL)';
        case G.VENDOR:
          return 'WebGPU';
        case G.RENDERER:
          return 'Observatory WebGPU';
        case G.MAX_TEXTURE_IMAGE_UNITS:
        case G.MAX_COMBINED_TEXTURE_IMAGE_UNITS:
          return Math.min(
            32,
            this.device.limits.maxSampledTexturesPerShaderStage,
          );
        case 0x84ff:
          return 16;
        case G.MAX_VERTEX_ATTRIBS:
          return 16;
        case G.MAX_TEXTURE_SIZE:
        case G.MAX_CUBE_MAP_TEXTURE_SIZE:
          return this.device.limits.maxTextureDimension2D;
        case G.MAX_3D_TEXTURE_SIZE:
          return this.device.limits.maxTextureDimension3D;
        case G.FRAMEBUFFER_BINDING:
          return this.drawFbo || null;
        case G.READ_FRAMEBUFFER_BINDING:
          return this.readFbo || null;
        case G.VERTEX_ARRAY_BINDING:
          return this.vao === this.defaultVao ? null : this.vao;
        case G.CURRENT_PROGRAM:
          return this.program || null;
        case G.ARRAY_BUFFER_BINDING:
          return this.buffers.get(G.ARRAY_BUFFER) || null;
        case G.PIXEL_PACK_BUFFER_BINDING:
          return null;
        case G.RENDERBUFFER_BINDING:
          return this.renderbuffer || null;
        default:
          throw new Error(
            `Unimplemented WebGPU parameter 0x${name.toString(16)}`,
          );
      }
    }
    getInternalformatParameter() {
      return new Int32Array([4, 1]);
    }
    getShaderPrecisionFormat() {
      return { rangeMin: 127, rangeMax: 127, precision: 23 };
    }
    enable(capability) {
      this.enabled.add(capability);
    }
    disable(capability) {
      this.enabled.delete(capability);
    }
    isEnabled(capability) {
      return this.enabled.has(capability);
    }
    clearColor(...color) {
      this.parameters.set(G.COLOR_CLEAR_VALUE, color);
    }
    clearDepth(value) {
      this.parameters.set(G.DEPTH_CLEAR_VALUE, value);
    }
    depthFunc(value) {
      this.parameters.set(G.DEPTH_FUNC, value);
    }
    depthMask(value) {
      this.parameters.set(G.DEPTH_WRITEMASK, value);
    }
    viewport(...value) {
      this.parameters.set(G.VIEWPORT, value);
    }
    scissor(...value) {
      this.parameters.set(G.SCISSOR_BOX, value);
    }
    polygonOffset(factor, units) {
      this.parameters.set(G.POLYGON_OFFSET_FACTOR, factor);
      this.parameters.set(G.POLYGON_OFFSET_UNITS, units);
    }
    blendFunc(source, destination) {
      this.blendFuncSeparate(source, destination, source, destination);
    }
    blendEquation(value) {
      this.blendEquationSeparate(value, value);
    }
    blendEquationSeparate(rgb, alpha) {
      if (rgb !== G.FUNC_ADD || alpha !== G.FUNC_ADD)
        throw new Error('Only additive blend equations are supported');
      this.parameters.set(G.BLEND_EQUATION_RGB, rgb);
      this.parameters.set(G.BLEND_EQUATION_ALPHA, alpha);
    }
    blendFuncSeparate(sr, dr, sa, da) {
      for (const [key, value] of [
        [G.BLEND_SRC_RGB, sr],
        [G.BLEND_DST_RGB, dr],
        [G.BLEND_SRC_ALPHA, sa],
        [G.BLEND_DST_ALPHA, da],
      ])
        this.parameters.set(key, value);
    }
    pixelStorei(name, value) {
      this.parameters.set(name, value);
    }
    createShader(type) {
      return object({ type });
    }
    shaderSource(shader, source) {
      shader.source = source;
    }
    compileShader() {} // Every supported program was compiled and checked offline.
    getShaderParameter(shader, name) {
      return name === G.COMPILE_STATUS;
    }
    getShaderInfoLog() {
      return '';
    }
    deleteShader() {}
    createProgram() {
      return object({
        shaders: [],
        uniforms: [],
        locations: new Map(),
        groups: new Map(),
      });
    }
    attachShader(program, shader) {
      program.shaders.push(shader);
    }
    linkProgram(program) {
      const vertex = program.shaders.find((s) => s.type === G.VERTEX_SHADER);
      const fragment = program.shaders.find(
        (s) => s.type === G.FRAGMENT_SHADER,
      );
      program.definition =
        this.shaders[
          sourceKey(vertex.source) + ':' + sourceKey(fragment.source)
        ];
      if (!program.definition) {
        program.error =
          'Shader source is absent from the offline WebGPU manifest. Rebuild webgpu-shaders.json.';
        return;
      }
      for (const stage of ['vert', 'frag']) {
        const definition = program.definition[stage];
        definition.module ||= this.device.createShaderModule({
          label: program.definition.label + ' ' + stage,
          code: definition.code,
        });
        program[stage] = {
          data: new ArrayBuffer(align(Math.max(16, definition.blockSize), 16)),
          definition,
        };
        for (const member of definition.members) {
          const base = member.name;
          if (!program.locations.has(base)) {
            const size = member.array?.[0] || 1;
            program.uniforms.push({
              name: size > 1 ? base + '[0]' : base,
              size,
              type: G.FLOAT,
            });
            program.locations.set(base, { program, base, stages: [] });
          }
          program.locations.get(base).stages.push({ stage, member });
        }
        for (const sampler of definition.samplers) {
          if (!program.locations.has(sampler.name)) {
            program.uniforms.push({
              name: sampler.name,
              size: 1,
              type: G.SAMPLER_2D,
            });
            program.locations.set(sampler.name, {
              program,
              base: sampler.name,
              stages: [],
              sampler: true,
              unit: 0,
            });
          }
        }
      }
      const globals = [],
        entries = [];
      for (const [binding, stage] of ['vert', 'frag'].entries()) {
        if (program[stage].definition.blockSize)
          globals.push({
            binding,
            visibility:
              stage === 'vert'
                ? GPUShaderStage.VERTEX
                : GPUShaderStage.FRAGMENT,
            buffer: {
              type: 'uniform',
              hasDynamicOffset: true,
              minBindingSize: program[stage].data.byteLength,
            },
          });
      }
      for (const sampler of program.definition.frag.samplers) {
        const shadow = sampler.type.endsWith('Shadow');
        const dimension = sampler.type.includes('Cube')
          ? 'cube'
          : sampler.type.includes('3D')
            ? '3d'
            : sampler.type.includes('Array')
              ? '2d-array'
              : '2d';
        entries.push({
          binding: sampler.binding,
          visibility: GPUShaderStage.FRAGMENT,
          texture: {
            sampleType: shadow ? 'depth' : 'float',
            viewDimension: dimension,
          },
        });
        if (sampler.sampled)
          entries.push({
            binding: sampler.binding + 1,
            visibility: GPUShaderStage.FRAGMENT,
            sampler: { type: shadow ? 'comparison' : 'filtering' },
          });
      }
      const globalLayout = this.device.createBindGroupLayout({
        entries: globals,
      });
      program.textureLayout = this.device.createBindGroupLayout({ entries });
      program.pipelineLayout = this.device.createPipelineLayout({
        bindGroupLayouts: [globalLayout, program.textureLayout],
      });
      if (globals.length)
        program.globals = this.device.createBindGroup({
          layout: globalLayout,
          entries: globals.map((entry) => ({
            binding: entry.binding,
            resource: {
              buffer: this.uniformBuffer,
              offset: 0,
              size: entry.buffer.minBindingSize,
            },
          })),
        });
    }
    getProgramParameter(program, name) {
      if (name === G.LINK_STATUS) return !program.error;
      if (name === G.ACTIVE_UNIFORMS) return program.uniforms.length;
      if (name === G.ACTIVE_ATTRIBUTES)
        return program.definition?.vert.inputs.length || 0;
      return 0;
    }
    getProgramInfoLog(program) {
      return program.error || '';
    }
    getActiveUniform(program, index) {
      return program.uniforms[index];
    }
    getUniformLocation(program, name) {
      const base = name.replace(/\[\d+\]$/, ''),
        location = program.locations.get(base);
      return location
        ? { ...location, index: Number(/\[(\d+)\]$/.exec(name)?.[1] || 0) }
        : null;
    }
    useProgram(program) {
      this.program = program;
    }
    deleteProgram(program) {
      // Pipelines retain program layouts. Reconfiguration must release them.
      this.pipelineCache.clear();
      program.groups.clear();
    }
    uniform(
      location,
      values,
      offset = 0,
      length = values.length - offset,
      matrix = 0,
    ) {
      if (!location) return;
      const entry = location.program.locations.get(location.base);
      if (entry.sampler) {
        entry.unit = values[offset];
        return;
      }
      for (const { stage, member } of entry.stages) {
        writeUniform(
          location.program[stage].data,
          member,
          values,
          offset,
          length,
          matrix,
          location.index,
        );
      }
    }
    uniform1f(l, a) {
      this.uniform(l, [a]);
    }
    uniform1i(l, a) {
      this.uniform(l, [a]);
    }
    uniform2f(l, a, b) {
      this.uniform(l, [a, b]);
    }
    uniform3f(l, a, b, c) {
      this.uniform(l, [a, b, c]);
    }
    uniform4f(l, a, b, c, d) {
      this.uniform(l, [a, b, c, d]);
    }
    uniform4i(l, a, b, c, d) {
      this.uniform(l, [a, b, c, d]);
    }
    uniform2fv(l, a, o = 0, n = a.length - o) {
      this.uniform(l, a, o, n);
    }
    uniform4fv(l, a, o = 0, n = a.length - o) {
      this.uniform(l, a, o, n);
    }
    uniformMatrix3fv(l, transpose, a, o = 0, n = a.length - o) {
      if (transpose) throw new Error('Transposed matrix upload');
      this.uniform(l, a, o, n, 3);
    }
    uniformMatrix4fv(l, transpose, a, o = 0, n = a.length - o) {
      if (transpose) throw new Error('Transposed matrix upload');
      this.uniform(l, a, o, n, 4);
    }
    createBuffer() {
      return object({ gpu: null });
    }
    bindBuffer(target, buffer) {
      if (target === G.ELEMENT_ARRAY_BUFFER) this.vao.element = buffer;
      else this.buffers.set(target, buffer);
    }
    bufferData(target, data, usage, offset = 0, length) {
      const buffer =
        target === G.ELEMENT_ARRAY_BUFFER
          ? this.vao.element
          : this.buffers.get(target);
      if (!buffer) throw new Error('Missing bound WebGPU buffer');
      const bytes =
        typeof data === 'number'
          ? data
          : (length ?? data.length - offset) * data.BYTES_PER_ELEMENT;
      if (buffer.gpu) this.retired.push(buffer.gpu);
      buffer.gpu = this.device.createBuffer({
        size: Math.max(4, align(bytes, 4)),
        usage:
          GPUBufferUsage.VERTEX |
          GPUBufferUsage.INDEX |
          GPUBufferUsage.COPY_DST,
      });
      if (typeof data !== 'number')
        this.device.queue.writeBuffer(
          buffer.gpu,
          0,
          data.buffer,
          data.byteOffset + offset * data.BYTES_PER_ELEMENT,
          bytes,
        );
    }
    bufferSubData(
      target,
      destination,
      data,
      offset = 0,
      length = data.length - offset,
    ) {
      const buffer =
        target === G.ELEMENT_ARRAY_BUFFER
          ? this.vao.element
          : this.buffers.get(target);
      this.device.queue.writeBuffer(
        buffer.gpu,
        destination,
        data.buffer,
        data.byteOffset + offset * data.BYTES_PER_ELEMENT,
        length * data.BYTES_PER_ELEMENT,
      );
    }
    deleteBuffer(buffer) {
      if (buffer?.gpu) this.retired.push(buffer.gpu);
      for (const [target, bound] of this.buffers)
        if (bound === buffer) this.buffers.delete(target);
      if (buffer) buffer.gpu = null;
    }
    createVertexArray() {
      return object({ attributes: new Map(), element: null });
    }
    bindVertexArray(vao) {
      this.vao = vao || this.defaultVao;
    }
    deleteVertexArray() {}
    enableVertexAttribArray(index) {
      const a = this.vao.attributes.get(index) || {};
      a.enabled = true;
      this.vao.attributes.set(index, a);
    }
    vertexAttribPointer(index, size, type, normalized, stride, offset) {
      this.vao.attributes.set(index, {
        enabled: this.vao.attributes.get(index)?.enabled,
        size,
        type,
        normalized,
        stride: stride || size * 4,
        offset,
        buffer: this.buffers.get(G.ARRAY_BUFFER),
      });
    }
    createTexture() {
      return object({
        params: new Map(),
        views: new Map(),
        generation: 0,
      });
    }
    activeTexture(unit) {
      this.unit = unit - G.TEXTURE0;
      this.parameters.set(G.ACTIVE_TEXTURE, unit);
    }
    bindTexture(target, texture) {
      this.textures[this.unit].set(target, texture);
      if (texture) texture.target = target;
    }
    texture(target) {
      return this.textures[this.unit].get(
        target >= G.TEXTURE_CUBE_MAP_POSITIVE_X &&
          target <= G.TEXTURE_CUBE_MAP_NEGATIVE_Z
          ? G.TEXTURE_CUBE_MAP
          : target,
      );
    }
    texParameteri(target, name, value) {
      this.texture(target).params.set(name, value);
    }
    texParameterf(target, name, value) {
      this.texParameteri(target, name, value);
    }
    allocate(
      texture,
      internal,
      width,
      height,
      depth,
      levels,
      sampleCount = 1,
      render3d = false,
    ) {
      const format = formats.get(internal);
      if (!format)
        throw new Error(
          `Unsupported WebGPU texture format 0x${internal.toString(16)}`,
        );
      texture.internal = internal;
      texture.format = format[0];
      texture.components = format[1];
      texture.bytes = format[2];
      texture.width = width;
      texture.height = height;
      texture.depth = depth;
      texture.levels = levels;
      texture.samples = sampleCount;
      const dimension = texture.target === G.TEXTURE_3D ? '3d' : '2d';
      // Sampled/storage noise volumes must not carry render-attachment usage.
      // Older Dawn attempts an invalid 2D lazy-clear view for those 3D mips.
      const usage = textureUsage(
        {
          dimension,
          levels,
          samples: sampleCount,
          compressed: texture.format.startsWith('bc'),
          render3d,
        },
        GPUTextureUsage,
      );
      if (texture.gpu) this.retired.push(texture.gpu);
      texture.gpu = this.device.createTexture({
        label: `texture ${texture.id} (${dimension} ${width}x${height}x${depth})`,
        size: [width, height, depth],
        dimension,
        format: texture.format,
        mipLevelCount: levels,
        sampleCount,
        usage,
      });
      // Older Dawn lazily clears an untouched renderable 3D texture through
      // an invalid 2D view when it is first sampled. Night starts and settings
      // rebuilds may not execute the solar-light passes at all. Initialize
      // every mip explicitly so a dark light cache is valid before sampling.
      if (dimension === '3d' && render3d) {
        for (let level = 0; level < levels; ++level) {
          const w = Math.max(1, width >> level),
            h = Math.max(1, height >> level),
            d = Math.max(1, depth >> level);
          const bytesPerRow = w * texture.components * texture.bytes;
          this.device.queue.writeTexture(
            { texture: texture.gpu, mipLevel: level },
            new Uint8Array(bytesPerRow * h * d),
            { bytesPerRow, rowsPerImage: h },
            [w, h, d],
          );
        }
      }
      ++texture.generation;
      texture.views.clear();
    }
    texImage2D(
      target,
      level,
      internal,
      width,
      height,
      border,
      format,
      type,
      data,
      offset = 0,
    ) {
      const texture = this.texture(target),
        cube = texture.target === G.TEXTURE_CUBE_MAP;
      if (!texture.gpu || (level === 0 && !cube)) {
        const levels =
          data || cube ? Math.floor(Math.log2(Math.max(width, height))) + 1 : 1;
        this.allocate(texture, internal, width, height, cube ? 6 : 1, levels);
      }
      if (data)
        this.upload(
          texture,
          level,
          [0, 0, cube ? target - G.TEXTURE_CUBE_MAP_POSITIVE_X : 0],
          [width, height, 1],
          format,
          type,
          data,
          offset,
        );
    }
    texImage3D(
      target,
      level,
      internal,
      width,
      height,
      depth,
      border,
      format,
      type,
      data,
      offset = 0,
    ) {
      const texture = this.texture(target);
      this.allocate(
        texture,
        internal,
        width,
        height,
        depth,
        data ? Math.floor(Math.log2(Math.max(width, height, depth))) + 1 : 1,
        1,
        !data,
      );
      if (data)
        this.upload(
          texture,
          level,
          [0, 0, 0],
          [width, height, depth],
          format,
          type,
          data,
          offset,
        );
    }
    texStorage2D(target, levels, internal, width, height) {
      this.allocate(
        this.texture(target),
        internal,
        width,
        height,
        target === G.TEXTURE_CUBE_MAP ? 6 : 1,
        levels,
      );
    }
    texStorage3D(target, levels, internal, width, height, depth) {
      this.allocate(
        this.texture(target),
        internal,
        width,
        height,
        depth,
        levels,
      );
    }
    texSubImage3D(
      target,
      level,
      x,
      y,
      z,
      width,
      height,
      depth,
      format,
      type,
      data,
      offset = 0,
    ) {
      this.upload(
        this.texture(target),
        level,
        [x, y, z],
        [width, height, depth],
        format,
        type,
        data,
        offset,
      );
    }
    compressedTexImage2D(
      target,
      level,
      internal,
      width,
      height,
      border,
      data,
      offset = 0,
      length = data.byteLength - offset,
    ) {
      const texture = this.texture(target);
      if (!texture.gpu)
        this.allocate(
          texture,
          internal,
          width,
          height,
          6,
          Math.floor(Math.log2(width)) + 1,
        );
      this.device.queue.writeTexture(
        {
          texture: texture.gpu,
          mipLevel: level,
          origin: [0, 0, target - G.TEXTURE_CUBE_MAP_POSITIVE_X],
        },
        new Uint8Array(data.buffer, data.byteOffset + offset, length),
        {
          bytesPerRow: Math.ceil(width / 4) * 16,
          rowsPerImage: Math.ceil(height / 4),
        },
        [align(width, 4), align(height, 4), 1],
      );
    }
    upload(texture, level, origin, size, format, type, data, offset) {
      const inputComponents =
        format === G.RED ? 1 : format === G.RG ? 2 : format === G.RGB ? 3 : 4;
      const [width, height, depth] = size;
      const row = align(
        width * inputComponents * data.BYTES_PER_ELEMENT,
        this.parameters.get(G.UNPACK_ALIGNMENT),
      );
      const direct =
        inputComponents === texture.components &&
        (type !== G.FLOAT || texture.bytes === 4) &&
        row === width * texture.components * texture.bytes;
      let pixels;
      if (direct)
        pixels = new Uint8Array(
          data.buffer,
          data.byteOffset + offset * data.BYTES_PER_ELEMENT,
          width * height * depth * texture.components * texture.bytes,
        );
      else {
        const count = width * height * depth;
        pixels =
          texture.bytes === 2
            ? new Uint16Array(count * texture.components)
            : new Uint8Array(count * texture.components);
        for (let z = 0; z < depth; ++z)
          for (let y = 0; y < height; ++y)
            for (let x = 0; x < width; ++x)
              for (let c = 0; c < texture.components; ++c) {
                const dest =
                  ((z * height + y) * width + x) * texture.components + c;
                const src =
                  offset +
                  ((z * height + y) * row) / data.BYTES_PER_ELEMENT +
                  x * inputComponents +
                  c;
                pixels[dest] =
                  c >= inputComponents
                    ? texture.bytes === 2
                      ? half(1)
                      : 255
                    : type === G.FLOAT && texture.bytes === 2
                      ? half(data[src])
                      : data[src];
              }
      }
      this.device.queue.writeTexture(
        { texture: texture.gpu, mipLevel: level, origin },
        pixels,
        {
          bytesPerRow: width * texture.components * texture.bytes,
          rowsPerImage: height,
        },
        size,
      );
    }
    generateMipmap(target) {
      this.endPass();
      const texture = this.texture(target);
      if (texture.levels <= 1) return;
      if (target === G.TEXTURE_3D) {
        this.mip3d ||= new Map();
        let pipeline = this.mip3d.get(texture.format);
        if (!pipeline) {
          const code = `@group(0) @binding(0) var source:texture_3d<f32>;
@group(0) @binding(1) var destination:texture_storage_3d<${texture.format},write>;
@compute @workgroup_size(4,4,4) fn main(@builtin(global_invocation_id) id:vec3<u32>){
 let size=textureDimensions(destination);if(any(id>=size)){return;}
 let sourceSize=vec3<i32>(textureDimensions(source));var sum=vec4<f32>(0.);
 for(var z=0;z<2;z++){for(var y=0;y<2;y++){for(var x=0;x<2;x++){
 sum+=textureLoad(source,min(vec3<i32>(id)*2+vec3<i32>(x,y,z),sourceSize-1),0);}}}
 textureStore(destination,vec3<i32>(id),sum*.125);}`;
          pipeline = this.device.createComputePipeline({
            layout: 'auto',
            compute: {
              module: this.device.createShaderModule({ code }),
              entryPoint: 'main',
            },
          });
          this.mip3d.set(texture.format, pipeline);
        }
        for (let level = 1; level < texture.levels; ++level) {
          const group = this.device.createBindGroup({
            layout: pipeline.getBindGroupLayout(0),
            entries: [
              { binding: 0, resource: this.view(texture, '3d', level - 1, 1) },
              { binding: 1, resource: this.view(texture, '3d', level, 1) },
            ],
          });
          const pass = this.commands().beginComputePass();
          pass.setPipeline(pipeline);
          pass.setBindGroup(0, group);
          pass.dispatchWorkgroups(
            Math.ceil(Math.max(1, texture.width >> level) / 4),
            Math.ceil(Math.max(1, texture.height >> level) / 4),
            Math.ceil(Math.max(1, texture.depth >> level) / 4),
          );
          pass.end();
        }
      } else {
        this.mip2d ||= new Map();
        let pipeline = this.mip2d.get(texture.format);
        if (!pipeline) {
          const code = `struct Output{@builtin(position) p:vec4<f32>,@location(0) uv:vec2<f32>};
@vertex fn vertex(@builtin(vertex_index) i:u32)->Output{let p=vec2<f32>(f32((i<<1u)&2u),f32(i&2u));return Output(vec4<f32>(p*2.-1.,0.,1.),vec2<f32>(p.x,1.-p.y));}
@group(0) @binding(0) var image:texture_2d<f32>;@group(0) @binding(1) var filtering:sampler;
@fragment fn fragment(input:Output)->@location(0) vec4<f32>{return textureSample(image,filtering,input.uv);}`;
          const module = this.device.createShaderModule({ code });
          pipeline = this.device.createRenderPipeline({
            layout: 'auto',
            vertex: { module, entryPoint: 'vertex' },
            fragment: {
              module,
              entryPoint: 'fragment',
              targets: [{ format: texture.format }],
            },
            primitive: { topology: 'triangle-list' },
          });
          this.mip2d.set(texture.format, pipeline);
        }
        const sampler = this.device.createSampler({
          minFilter: 'linear',
          magFilter: 'linear',
        });
        for (let level = 1; level < texture.levels; ++level)
          for (let layer = 0; layer < texture.depth; ++layer) {
            const group = this.device.createBindGroup({
              layout: pipeline.getBindGroupLayout(0),
              entries: [
                {
                  binding: 0,
                  resource: this.view(texture, '2d', level - 1, 1, layer),
                },
                { binding: 1, resource: sampler },
              ],
            });
            const pass = this.commands().beginRenderPass({
              colorAttachments: [
                {
                  view: this.view(texture, '2d', level, 1, layer),
                  loadOp: 'clear',
                  storeOp: 'store',
                },
              ],
            });
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, group);
            pass.draw(3);
            pass.end();
          }
      }
    }
    deleteTexture(texture) {
      if (texture?.gpu) this.retired.push(texture.gpu);
      for (const units of this.textures)
        for (const [target, bound] of units)
          if (bound === texture) units.delete(target);
      if (texture) {
        texture.gpu = null;
        texture.views.clear();
      }
    }
    view(texture, dimension, level = 0, count = texture.levels, layer) {
      const key = [dimension, level, count, layer].join(':');
      if (!texture.views.has(key))
        texture.views.set(
          key,
          texture.gpu.createView({
            dimension,
            baseMipLevel: level,
            mipLevelCount: count,
            ...(layer === undefined
              ? {}
              : { baseArrayLayer: layer, arrayLayerCount: 1 }),
          }),
        );
      return texture.views.get(key);
    }
    dummyTexture(target, shadow) {
      this.dummyTextures ||= new Map();
      const key = `${target}:${shadow}`;
      if (!this.dummyTextures.has(key)) {
        const texture = this.createTexture();
        texture.target = target;
        const depth = target === G.TEXTURE_CUBE_MAP ? 6 : 1;
        this.allocate(
          texture,
          shadow ? G.DEPTH_COMPONENT24 : G.RGBA8,
          1,
          1,
          depth,
          1,
        );
        texture.params.set(G.TEXTURE_MIN_FILTER, G.NEAREST);
        texture.params.set(G.TEXTURE_MAG_FILTER, G.NEAREST);
        if (!shadow)
          this.device.queue.writeTexture(
            { texture: texture.gpu },
            Uint8Array.from({ length: 4 * depth }, (_, i) =>
              i % 4 === 3 ? 255 : 0,
            ),
            { bytesPerRow: 4, rowsPerImage: 1 },
            [1, 1, depth],
          );
        this.dummyTextures.set(key, texture);
      }
      return this.dummyTextures.get(key);
    }
    sampler(texture, comparison) {
      const p = texture.params;
      const nearest = p.get(G.TEXTURE_MAG_FILTER) === G.NEAREST;
      const desc = {
        maxAnisotropy: Math.min(16, Math.max(1, p.get(0x84fe) || 1)),
        magFilter: nearest ? 'nearest' : 'linear',
        minFilter:
          p.get(G.TEXTURE_MIN_FILTER) === G.NEAREST ? 'nearest' : 'linear',
        mipmapFilter:
          p.get(G.TEXTURE_MIN_FILTER) === G.LINEAR_MIPMAP_LINEAR
            ? 'linear'
            : 'nearest',
        addressModeU:
          p.get(G.TEXTURE_WRAP_S) === G.REPEAT ? 'repeat' : 'clamp-to-edge',
        addressModeV:
          p.get(G.TEXTURE_WRAP_T) === G.REPEAT ? 'repeat' : 'clamp-to-edge',
        addressModeW:
          p.get(G.TEXTURE_WRAP_R) === G.REPEAT ? 'repeat' : 'clamp-to-edge',
        lodMaxClamp: [G.LINEAR, G.NEAREST].includes(p.get(G.TEXTURE_MIN_FILTER))
          ? 0
          : 32,
        ...(comparison ? { compare: 'less-equal' } : {}),
      };
      const key = JSON.stringify(desc);
      if (!this.samplerCache.has(key))
        this.samplerCache.set(key, this.device.createSampler(desc));
      return this.samplerCache.get(key);
    }
    createFramebuffer() {
      return object({
        attachments: new Map(),
        outputs: [G.COLOR_ATTACHMENT0],
        read: G.COLOR_ATTACHMENT0,
      });
    }
    bindFramebuffer(target, framebuffer) {
      this.endPass();
      if (target !== G.READ_FRAMEBUFFER) this.drawFbo = framebuffer;
      if (target !== G.DRAW_FRAMEBUFFER) this.readFbo = framebuffer;
    }
    framebufferTexture2D(target, attachment, textureTarget, texture, level) {
      this.endPass();
      this.drawFbo.attachments.set(attachment, { texture, level, layer: 0 });
    }
    framebufferTextureLayer(target, attachment, texture, level, layer) {
      this.endPass();
      this.drawFbo.attachments.set(attachment, { texture, level, layer });
    }
    framebufferRenderbuffer(target, attachment, type, texture) {
      this.endPass();
      this.drawFbo.attachments.set(attachment, { texture, level: 0, layer: 0 });
    }
    checkFramebufferStatus() {
      return G.FRAMEBUFFER_COMPLETE;
    }
    deleteFramebuffer() {}
    createRenderbuffer() {
      return this.createTexture();
    }
    bindRenderbuffer(target, renderbuffer) {
      this.renderbuffer = renderbuffer;
    }
    renderbufferStorage(target, internal, width, height) {
      this.allocate(this.renderbuffer, internal, width, height, 1, 1);
    }
    renderbufferStorageMultisample(target, samples, internal, width, height) {
      this.allocate(this.renderbuffer, internal, width, height, 1, 1, samples);
    }
    deleteRenderbuffer(texture) {
      this.deleteTexture(texture);
    }
    drawBuffers(outputs) {
      this.endPass();
      this.drawFbo.outputs = [...outputs];
    }
    readBuffer(output) {
      this.readFbo.read = output;
    }
    attachmentView(attachment) {
      const { texture, level, layer } = attachment;
      if (texture.target === G.TEXTURE_3D)
        return this.view(texture, '3d', level, 1);
      return this.view(texture, '2d', level, 1, layer);
    }
    beginPass(clear = 0) {
      if (this.pass && !clear) return this.pass;
      this.endPass();
      this.configure();
      const fbo = this.drawFbo;
      const color = this.parameters.get(G.COLOR_CLEAR_VALUE);
      let attachments;
      if (fbo)
        attachments = fbo.outputs.map((output) =>
          output === G.NONE
            ? null
            : {
                view: this.attachmentView(fbo.attachments.get(output)),
                ...(fbo.attachments.get(output).texture.target === G.TEXTURE_3D
                  ? { depthSlice: fbo.attachments.get(output).layer }
                  : {}),
                loadOp: clear & G.COLOR_BUFFER_BIT ? 'clear' : 'load',
                storeOp: 'store',
                clearValue: color,
              },
        );
      else {
        this.swapTexture ||= this.context.getCurrentTexture();
        attachments = [
          {
            view: this.swapTexture.createView(),
            loadOp: clear & G.COLOR_BUFFER_BIT ? 'clear' : 'load',
            storeOp: 'store',
            clearValue: color,
          },
        ];
      }
      const depth = fbo?.attachments.get(G.DEPTH_ATTACHMENT);
      const descriptor = { colorAttachments: attachments };
      if (depth)
        descriptor.depthStencilAttachment = {
          view: this.attachmentView(depth),
          depthLoadOp: clear & G.DEPTH_BUFFER_BIT ? 'clear' : 'load',
          depthStoreOp: 'store',
          depthClearValue: this.parameters.get(G.DEPTH_CLEAR_VALUE),
        };
      this.passFbo = fbo;
      this.pass = this.commands().beginRenderPass(descriptor);
      return this.pass;
    }
    clear(mask) {
      this.beginPass(mask);
      this.endPass();
    }
    blitFramebuffer(x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask) {
      this.endPass();
      if (
        x0 ||
        y0 ||
        dx0 ||
        dy0 ||
        x1 !== dx1 ||
        y1 !== dy1 ||
        mask !== G.COLOR_BUFFER_BIT
      )
        throw new Error('Only full-size color resolves are supported');
      const source = this.readFbo.attachments.get(this.readFbo.read);
      const destination = this.drawFbo.attachments.get(
        this.drawFbo.outputs.find((v) => v !== G.NONE),
      );
      const pass = this.commands().beginRenderPass({
        colorAttachments: [
          {
            view: this.attachmentView(source),
            resolveTarget: this.attachmentView(destination),
            loadOp: 'load',
            storeOp: 'store',
          },
        ],
      });
      pass.end();
    }
    pipeline(mode) {
      const program = this.program,
        definition = program.definition;
      const fbo = this.drawFbo;
      const targets = fbo
        ? fbo.outputs.map((o) =>
            o === G.NONE
              ? null
              : { format: fbo.attachments.get(o).texture.format },
          )
        : [{ format: this.format }];
      if (this.enabled.has(G.BLEND))
        for (const target of targets)
          if (target)
            target.blend = {
              color: {
                srcFactor: blendFactor(this.parameters.get(G.BLEND_SRC_RGB)),
                dstFactor: blendFactor(this.parameters.get(G.BLEND_DST_RGB)),
                operation: 'add',
              },
              alpha: {
                srcFactor: blendFactor(this.parameters.get(G.BLEND_SRC_ALPHA)),
                dstFactor: blendFactor(this.parameters.get(G.BLEND_DST_ALPHA)),
                operation: 'add',
              },
            };
      const inputs = definition.vert.inputs.filter(
        (input) => input.location !== undefined,
      );
      const attributes = inputs.map((input) => {
        const attribute = this.vao.attributes.get(input.location);
        if (!attribute?.enabled || attribute.type !== G.FLOAT)
          throw new Error(`Missing float vertex attribute ${input.name}`);
        return {
          arrayStride: attribute.stride,
          stepMode: definition.points ? 'instance' : 'vertex',
          attributes: [
            {
              shaderLocation: input.location,
              offset: attribute.offset,
              format:
                attribute.size === 1 ? 'float32' : 'float32x' + attribute.size,
            },
          ],
        };
      });
      const depth = fbo?.attachments.get(G.DEPTH_ATTACHMENT);
      const depthTest = this.enabled.has(G.DEPTH_TEST);
      const descriptor = {
        layout: program.pipelineLayout,
        vertex: {
          module: definition.vert.module,
          entryPoint: 'main',
          buffers: attributes,
        },
        fragment: {
          module: definition.frag.module,
          entryPoint: 'main',
          targets,
        },
        primitive: {
          topology: definition.points ? 'triangle-strip' : 'triangle-list',
          cullMode: 'none',
        },
        multisample: {
          count:
            fbo?.attachments.get(fbo.outputs.find((v) => v !== G.NONE))?.texture
              .samples ||
            depth?.texture.samples ||
            1,
        },
      };
      if (depth)
        descriptor.depthStencil = {
          format: depth.texture.format,
          depthWriteEnabled:
            depthTest && this.parameters.get(G.DEPTH_WRITEMASK),
          depthCompare: depthTest
            ? this.parameters.get(G.DEPTH_FUNC) === G.LEQUAL
              ? 'less-equal'
              : 'less'
            : 'always',
          depthBias: this.enabled.has(G.POLYGON_OFFSET_FILL)
            ? this.parameters.get(G.POLYGON_OFFSET_UNITS)
            : 0,
          depthBiasSlopeScale: this.enabled.has(G.POLYGON_OFFSET_FILL)
            ? this.parameters.get(G.POLYGON_OFFSET_FACTOR)
            : 0,
        };
      const key = JSON.stringify([
        program.id,
        attributes,
        targets,
        descriptor.multisample,
        descriptor.depthStencil,
      ]);
      if (!this.pipelineCache.has(key)) {
        if (this.pipelineCache.size >= 256)
          throw new Error('WebGPU pipeline cache limit exceeded');
        this.pipelineCache.set(
          key,
          this.device.createRenderPipeline({
            label: definition.label,
            ...descriptor,
          }),
        );
      }
      return { pipeline: this.pipelineCache.get(key), inputs };
    }
    bindDraw(mode) {
      const { pipeline, inputs } = this.pipeline(mode),
        pass = this.beginPass();
      pass.setPipeline(pipeline);
      const [x, y, w, h] = this.parameters.get(G.VIEWPORT);
      pass.setViewport(x, this.drawFbo ? y : this.height - y - h, w, h, 0, 1);
      const colorAttachment = this.drawFbo?.attachments.get(
        this.drawFbo.outputs.find((output) => output !== G.NONE),
      );
      const attachment =
        colorAttachment || this.drawFbo?.attachments.get(G.DEPTH_ATTACHMENT);
      const extentWidth = attachment
        ? Math.max(1, attachment.texture.width >> attachment.level)
        : this.width;
      const extentHeight = attachment
        ? Math.max(1, attachment.texture.height >> attachment.level)
        : this.height;
      if (this.enabled.has(G.SCISSOR_TEST)) {
        const [sx, sy, sw, sh] = this.parameters.get(G.SCISSOR_BOX);
        const top = this.drawFbo ? sy : extentHeight - sy - sh;
        const left = Math.max(0, Math.min(extentWidth, sx));
        const bottom = Math.max(0, Math.min(extentHeight, top));
        pass.setScissorRect(
          left,
          bottom,
          Math.max(0, Math.min(extentWidth, sx + sw) - left),
          Math.max(0, Math.min(extentHeight, top + sh) - bottom),
        );
      } else {
        // WebGPU has no disabled scissor state. Reset its persistent pass
        // rectangle when GLES disables scissoring after drawing a body.
        pass.setScissorRect(0, 0, extentWidth, extentHeight);
      }

      inputs.forEach((input, slot) =>
        pass.setVertexBuffer(
          slot,
          this.vao.attributes.get(input.location).buffer.gpu,
        ),
      );
      const program = this.program,
        dynamic = [];
      for (const stage of ['vert', 'frag']) {
        const data = program[stage].data;
        if (!program[stage].definition.blockSize) continue;
        const offset = align(this.uniformCursor, 256);
        if (offset + data.byteLength > this.uniformBytes.byteLength)
          throw new Error('WebGPU uniform arena exhausted');
        new Uint8Array(this.uniformBytes, offset, data.byteLength).set(
          new Uint8Array(data),
        );
        this.uniformCursor = offset + data.byteLength;
        dynamic.push(offset);
      }
      if (program.globals) pass.setBindGroup(0, program.globals, dynamic);
      const bindings = [],
        identities = [];
      for (const sampler of program.definition.frag.samplers) {
        const unit = program.locations.get(sampler.name).unit;
        const target = sampler.type.includes('Cube')
          ? G.TEXTURE_CUBE_MAP
          : sampler.type.includes('3D')
            ? G.TEXTURE_3D
            : sampler.type.includes('Array')
              ? G.TEXTURE_2D_ARRAY
              : G.TEXTURE_2D;
        const texture =
          this.textures[unit].get(target) ||
          this.dummyTexture(target, sampler.type.endsWith('Shadow'));
        bindings.push({
          binding: sampler.binding,
          resource: this.view(
            texture,
            target === G.TEXTURE_CUBE_MAP
              ? 'cube'
              : target === G.TEXTURE_3D
                ? '3d'
                : target === G.TEXTURE_2D_ARRAY
                  ? '2d-array'
                  : '2d',
          ),
        });
        if (sampler.sampled)
          bindings.push({
            binding: sampler.binding + 1,
            resource: this.sampler(texture, sampler.type.endsWith('Shadow')),
          });
        identities.push([texture.id, texture.generation, [...texture.params]]);
      }
      if (bindings.length) {
        const key = JSON.stringify(identities);
        if (!program.groups.has(key)) {
          if (program.groups.size >= 64)
            program.groups.delete(program.groups.keys().next().value);
          program.groups.set(
            key,
            this.device.createBindGroup({
              layout: program.textureLayout,
              entries: bindings,
            }),
          );
        }
        pass.setBindGroup(1, program.groups.get(key));
      }
      return pass;
    }
    drawArrays(mode, first, count) {
      const pass = this.bindDraw(mode);
      if (mode === G.POINTS) pass.draw(4, count, 0, first);
      else if (mode === G.TRIANGLES) pass.draw(count, 1, first);
      else throw new Error(`Unsupported topology ${mode}`);
    }
    drawElements(mode, count, type, offset) {
      if (mode !== G.TRIANGLES || type !== G.UNSIGNED_INT)
        throw new Error('Only uint32 triangle indices supported');
      const pass = this.bindDraw(mode);
      pass.setIndexBuffer(this.vao.element.gpu, 'uint32');
      pass.drawIndexed(count, 1, offset / 4);
    }
    fenceSync() {
      const sync = object({ done: false });
      this.submit().then(() => {
        sync.done = true;
      });
      return sync;
    }
    clientWaitSync(sync) {
      return sync.done ? G.ALREADY_SIGNALED : G.TIMEOUT_EXPIRED;
    }
    deleteSync() {}
    flush() {} // fenceSync submits the complete command buffer.
    finish() {
      throw new Error(
        'Synchronous WebGPU waits are unavailable. Use asynchronous frame metrics.',
      );
    }
  }

  for (const name of Object.getOwnPropertyNames(G)) {
    if (typeof G[name] === 'number')
      Object.defineProperty(ObservatoryGPU.prototype, name, { value: G[name] });
  }
  function unavailable(message) {
    const error = new Error(message);
    error.webgpuUnavailable = true;
    return error;
  }
  globalThis.createObservatoryGPU = async (canvas, error) => {
    if (!navigator.gpu)
      throw unavailable('WebGPU is unavailable in this browser.');
    let adapter, device;
    try {
      adapter = await navigator.gpu.requestAdapter({
        powerPreference: 'high-performance',
      });
      if (!adapter) throw new Error('No WebGPU adapter is available.');
      const requiredFeatures = ['texture-compression-bc'].filter((feature) =>
        adapter.features.has(feature),
      );
      device = await adapter.requestDevice({
        requiredFeatures,
        requiredLimits: {
          maxSampledTexturesPerShaderStage: Math.min(
            32,
            adapter.limits.maxSampledTexturesPerShaderStage,
          ),
          maxSamplersPerShaderStage: Math.min(
            32,
            adapter.limits.maxSamplersPerShaderStage,
          ),
        },
      });
    } catch (failure) {
      throw unavailable(failure.message);
    }
    let renderer;
    try {
      const response = await fetch('webgpu-shaders.json');
      if (!response.ok)
        throw new Error('The offline WebGPU shader manifest is missing.');
      const shaders = await response.json();
      const context = canvas.getContext('webgpu');
      if (!context) throw new Error('Cannot create a WebGPU canvas.');
      renderer = new ObservatoryGPU(canvas, device, context, shaders);
      renderer.softwareAdapter =
        ObservatoryGraphicsData.softwareAdapter(adapter);
    } catch (failure) {
      device.destroy();
      throw failure;
    }
    device.addEventListener('uncapturederror', (event) => {
      console.error(event.error.message);
      error(event.error.message);
    });
    device.lost.then((info) => error('WebGPU device lost: ' + info.message));
    return renderer;
  };
})();
