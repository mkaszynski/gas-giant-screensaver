'use strict';
// Pure data conversions shared by the browser command adapter and Node tests.
((root) => {
  function sourceKey(text) {
    text = text.replace(/\r\n?/g, '\n');
    let hash = 2166136261;
    for (let i = 0; i < text.length; ++i)
      hash = Math.imul(hash ^ text.charCodeAt(i), 16777619);
    return (hash >>> 0).toString(16).padStart(8, '0');
  }
  const float = new Float32Array(1),
    bits = new Uint32Array(float.buffer);
  function float16(value) {
    float[0] = value;
    const word = bits[0],
      sign = (word >>> 16) & 0x8000;
    let exponent = ((word >>> 23) & 255) - 112;
    let mantissa = word & 0x7fffff;
    if (exponent === 143) return sign | 0x7c00 | (mantissa ? 0x200 : 0);
    if (exponent >= 31) return sign | 0x7c00;
    if (exponent < -10) return sign;
    if (exponent <= 0) {
      mantissa |= 0x800000;
      const shift = 14 - exponent,
        base = mantissa >>> shift;
      const remainder = mantissa & ((1 << shift) - 1),
        halfway = 1 << (shift - 1);
      return (
        sign |
        (base +
          (remainder > halfway || (remainder === halfway && base & 1) ? 1 : 0))
      );
    }
    // Addition carries rounded mantissa overflow into the exponent.
    return (
      sign |
      ((exponent << 10) + ((mantissa + 0xfff + ((mantissa >>> 13) & 1)) >>> 13))
    );
  }
  function writeUniform(
    data,
    member,
    values,
    offset,
    length,
    matrix = 0,
    index = 0,
  ) {
    const view = new DataView(data);
    const type = member.valueType || member.type;
    const components = matrix
      ? matrix * matrix
      : Number(/vec(\d)/.exec(type)?.[1] || 1);
    const stride =
      member.array_stride || (matrix ? matrix * 16 : components * 4);
    const integers = /^(u?int|[iu]vec|bool)/.test(type);
    for (let i = 0; i < length; ++i) {
      const element = Math.floor(i / components) + index,
        component = i % components;
      const byte =
        member.offset +
        element * stride +
        (matrix
          ? Math.floor(component / matrix) * (member.matrix_stride || 16) +
            (component % matrix) * 4
          : component * 4);
      if (integers) view.setInt32(byte, values[offset + i], true);
      else view.setFloat32(byte, values[offset + i], true);
    }
  }
  function textureUsage(
    { dimension, levels, samples, compressed, render3d },
    flags,
  ) {
    if (samples > 1) return flags.RENDER_ATTACHMENT;
    let usage = flags.TEXTURE_BINDING | flags.COPY_DST | flags.COPY_SRC;
    if (dimension === '3d') {
      if (levels > 1) usage |= flags.STORAGE_BINDING;
      if (render3d) usage |= flags.RENDER_ATTACHMENT;
    } else if (!compressed) usage |= flags.RENDER_ATTACHMENT;
    return usage;
  }
  const data = { sourceKey, float16, writeUniform, textureUsage };
  if (typeof module === 'object') module.exports = data;
  else root.ObservatoryGraphicsData = data;
})(globalThis);
