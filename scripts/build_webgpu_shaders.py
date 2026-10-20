#!/usr/bin/env python3
"""Compile the shared GLSL programs to WGSL offline; no runtime shader compiler.

Requires glslangValidator, spirv-cross, Node, and checksum-pinned twgsl tools.
The manifest is keyed by the original program sources, so an unbaked shader
change fails explicitly rather than accidentally using a stale pipeline.
"""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import urllib.request
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TWGSL_REVISION = "1431d93dbd9dd6051575fafcc43db9806d0483c4"
TWGSL_SHA256 = {
    "twgsl.js": "b4f1f66263b801210f955f74aa71f1939be647ce0fd80ea5befd12a78499d5ff",
    "twgsl.wasm": "a434c2decdbb38caadf5f486d806384a1543554828c983c77922b2d92579914c",
}


def source(name):
    text = "".join(
        line + "\n" for line in (ROOT / "shaders" / name).read_text().splitlines()
    )
    return re.sub(r'^#include "([^"]+)"\n', lambda m: source(m[1]), text, flags=re.M)


def source_key(text):
    # Preserve token and preprocessor-line boundaries. Only normalize line
    # endings; whitespace or comment edits must invalidate stale shader data.
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    units = text.encode("utf-16-le")
    value = 2166136261
    for index in range(0, len(units), 2):
        unit = units[index] | units[index + 1] << 8
        value = ((value ^ unit) * 16777619) & 0xFFFFFFFF
    return f"{value:08x}"


def programs():
    scaled = {
        "background.frag",
        "body.frag",
        "stars.frag",
        "rings.frag",
        "emission.frag",
        "meteors.frag",
    }
    for frag, vert, define in [
        *[
            (f + ".frag", "fullscreen.vert", "")
            for f in (
                "transmittance",
                "multiple",
                "sky_lut",
                "background",
                "body",
                "rings",
                "weather",
                "weather-light",
                "weather-rain",
                "weather-filter-probe",
                "presentation",
            )
        ],
        (
            "body.frag",
            "fullscreen.vert",
            "#define GIANT_ATMOSPHERE\n#define GIANT_SLAB_LUT\n#define GIANT_INTERIOR\n",
        ),
        (
            "body.frag",
            "fullscreen.vert",
            "#define GIANT_ATMOSPHERE\n#define GIANT_SLAB_LUT\n#define GIANT_LIMB\n",
        ),
        ("debug.frag", "debug.vert", ""),
        ("stars.frag", "stars.vert", ""),
        ("emission.frag", "emission.vert", ""),
        ("meteors.frag", "meteors.vert", ""),
        # Match SurfaceRenderer::link's byte-exact insertion order: material
        # precedes the fixed cloud model. Shadows use link's default mode 1.
        *[
            (
                "surface.frag",
                "surface.vert",
                material + f"#define OBSERVATORY_WEATHER_MODE {weather}\n",
            )
            for weather in (0, 1)
            for material in (
                "",
                "#define SURFACE_REFLECTION\n",
                "#define SURFACE_DEEP_WATER\n",
                "#define SURFACE_PURE_LAND\n",
            )
        ],
        *[
            (
                "post.frag",
                "fullscreen.vert",
                f"#define OBSERVATORY_WEATHER_MODE {weather}\n",
            )
            for weather in (0, 1)
        ],
        (
            "surface_shadow.frag",
            "surface_shadow.vert",
            "#define OBSERVATORY_WEATHER_MODE 1\n",
        ),
    ]:
        fragment = source(frag)
        if define:
            fragment = fragment.replace("\n", "\n" + define, 1)
        if frag in scaled:
            fragment = fragment.replace("\n", "\nuniform highp float uSceneScale;\n", 1)
            fragment = fragment.replace("void main()", "void scenePixel()", 1)
            fragment += "\nvoid main(){scenePixel();color.rgb=min(color.rgb*uSceneScale,vec3(60000.));}\n"
        flags = ",".join(line.removeprefix("#define ") for line in define.splitlines())
        yield (
            frag + (":" + flags if flags else ""),
            source(vert),
            fragment,
        )


TOOLS = {}


def run(*args):
    args = (TOOLS.get(args[0], args[0]), *args[1:])
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


DECL = re.compile(
    r"(?:(layout\s*\([^)]*\))\s*)?(flat\s+)?(in|out)\s+(\w+)\s+([\w,\s]+);"
)
UNIFORM = re.compile(r"\buniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([^;{}]+);")


def closing_paren(text, start):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("Unbalanced shader parentheses")


def arguments(text):
    depth, start, result = 0, 0, []
    for index, char in enumerate(text):
        if char in "([":
            depth += 1
        elif char in ")]":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    return result + [text[start:].strip()]


def padded_vec2_name(typ, name):
    return re.fullmatch(r"(\w+)\s*\[[^\[\]]+\]", name) if typ == "vec2" else None


def pad_uniform_arrays(text, uniforms):
    """Represent std140 vec2 array elements with WGSL's natural vec4 stride.

    Only storage changes. Reads retain their original two components, and the
    manifest records the logical upload type separately from reflected storage.
    Reject whole-array uses rather than silently changing function signatures.
    """
    storage = []
    for typ, name in uniforms:
        padded = padded_vec2_name(typ, name)
        storage.append(("vec4" if padded else typ, name))
        if not padded:
            continue
        for match in reversed(list(re.finditer(r"\b" + padded[1] + r"\b", text))):
            start = match.end()
            while start < len(text) and text[start].isspace():
                start += 1
            if start == len(text) or text[start] != "[":
                raise ValueError("Padded uniform array must be indexed: " + padded[1])
            depth = 0
            for end in range(start, len(text)):
                depth += (text[end] == "[") - (text[end] == "]")
                if depth == 0:
                    text = text[: end + 1] + ".xy" + text[end + 1 :]
                    break
            else:
                raise ValueError("Unbalanced uniform array index: " + padded[1])
    return text, storage


def logical_uniform_types(original):
    return {
        padded[1]: typ
        for match in UNIFORM.finditer(original)
        for typ in [match[1]]
        for name in match[2].split(",")
        if (padded := padded_vec2_name(typ, name.strip()))
    }


def split_sampler_parameters(text):
    functions = {}
    for match in reversed(list(re.finditer(r"\b\w+\s+(\w+)\s*\(", text))):
        end = closing_paren(text, match.end() - 1)
        if not text[end + 1 :].lstrip().startswith("{"):
            continue
        params = arguments(text[match.end() : end])
        samplers = {}
        for i, param in enumerate(params):
            sampler = re.fullmatch(r"(?:highp\s+)?(sampler\w+)\s+(\w+)", param)
            if sampler:
                samplers[i] = sampler.groups()
        if samplers:
            functions[match[1]] = samplers
            # Sampler parameters only participate in texture operations or
            # further calls taking sampler parameters. Expand operations locally.
            body_start = text.index("{", end)
            depth, body_end = 1, body_start + 1
            while depth:
                depth += (text[body_end] == "{") - (text[body_end] == "}")
                body_end += 1
            body = text[body_start:body_end]
            for typ, name in samplers.values():
                body = re.sub(
                    r"\b(texture(?:Lod|Grad|Size)?|texelFetch)\(\s*" + name + r"\s*,",
                    lambda m: m[1]
                    + "("
                    + typ
                    + "("
                    + name
                    + "_texture,"
                    + name
                    + "_sampler),",
                    body,
                )
            text = text[:body_start] + body + text[body_end:]
    # Signatures are unchanged in the preceding pass. Transform calls from the
    # end so nested calls and earlier offsets cannot invalidate later spans.
    for function, samplers in functions.items():
        matches = list(re.finditer(r"\b" + function + r"\s*\(", text))
        for match in reversed(matches):
            end = closing_paren(text, match.end() - 1)
            args = arguments(text[match.end() : end])
            definition = text[end + 1 :].lstrip().startswith("{")
            for i, (typ, name) in sorted(samplers.items(), reverse=True):
                texture = "texture" + typ.removeprefix("sampler").removesuffix("Shadow")
                if definition:
                    args[i] = (
                        texture
                        + " "
                        + name
                        + "_texture,"
                        + ("samplerShadow" if typ.endswith("Shadow") else "sampler")
                        + " "
                        + name
                        + "_sampler"
                    )
                else:
                    value = args[i]
                    if not re.fullmatch(r"\w+", value):
                        raise ValueError("Non-identifier sampler argument: " + value)
                    args[i] = value + "_texture," + value + "_sampler"
            text = text[: match.end()] + ",".join(args) + text[end:]
    return text


def convert(text, stage, varyings, post=False, points=False):
    text = text.replace("#version 300 es", "#version 450")
    text = re.sub(r"precision\s+\w+\s+\w+;", "", text)
    # Preprocessing first resolves feature branches before collecting uniforms.
    with tempfile.TemporaryDirectory() as directory:
        file = Path(directory) / ("input." + stage)
        file.write_text(text)
        text = run("glslangValidator", "-E", "-S", stage, str(file))
    uniforms, samplers = [], []

    def uniform(m):
        typ, names = m.groups()
        for name in names.split(","):
            name = name.strip()
            (samplers if typ.startswith("sampler") else uniforms).append((typ, name))
        return ""

    text = UNIFORM.sub(uniform, text)
    text, uniforms = pad_uniform_arrays(text, uniforms)
    text = split_sampler_parameters(text)

    def interface(m):
        layout, flat, direction, typ, names = m.groups()
        result = []
        for name in names.split(","):
            name = name.strip()
            location = (
                varyings[name]
                if (stage == "vert" and direction == "out")
                or (stage == "frag" and direction == "in")
                else int(re.search(r"location\s*=\s*(\d+)", layout)[1]) if layout else 0
            )
            result.append(
                f"layout(location={location}) {flat or ''}{direction} {typ} {name};"
            )
        return "\n".join(result)

    text = DECL.sub(interface, text)
    header = ""
    if uniforms:
        header += f"layout(set=0,binding={0 if stage == 'vert' else 1},std140) uniform Globals {{\n"
        header += "".join(f"  {typ} {name};\n" for typ, name in uniforms) + "};\n"
    for i, (typ, name) in enumerate(samplers):
        if "[" in name:
            raise ValueError("Sampler arrays need explicit bindings")
        texture = "texture" + typ.removeprefix("sampler").removesuffix("Shadow")
        header += (
            f"layout(set=1,binding={2 * i}) uniform {texture} {name}_texture;\n"
            f"layout(set=1,binding={2 * i + 1}) uniform {'samplerShadow' if typ.endswith('Shadow') else 'sampler'} {name}_sampler;\n"
            f"#define {name} {typ}({name}_texture,{name}_sampler)\n"
        )
    if stage == "vert":
        text = text.replace("gl_VertexID", "gl_VertexIndex")
        if points:
            text = text.replace("gl_PointSize", "pointSize")
            header += "float pointSize;\n"
        text = text.replace("void main()", "void sharedMain()", 1)
        text += "\nvoid main(){sharedMain();"
        if points:
            text += "vec2 corner=vec2(float(gl_VertexIndex&1),float((gl_VertexIndex>>1)&1))*2.-1.;gl_Position.xy+=corner*pointSize/uResolution*gl_Position.w;"
        text += (
            "gl_Position.z=(gl_Position.z+gl_Position.w)*.5;"
            + ("" if post else "gl_Position.y=-gl_Position.y;")
            + "}\n"
        )
    elif post:
        # Render targets use bottom-row-first coordinates; only the final
        # swapchain has a physical top-left origin.
        text = text.replace(
            "gl_FragCoord",
            "vec4(gl_FragCoord.x,uResolution.y-gl_FragCoord.y,gl_FragCoord.zw)",
        )
    return text.replace("#version 450", "#version 450\n" + header, 1), samplers


def convert_program(label, original, stage, varyings):
    name = label.split(":", 1)[0]
    return convert(
        original,
        stage,
        varyings,
        # Browser composites always use a budget-sized offscreen target.
        # Only presentation and the overlay write directly to the swapchain.
        post=name in ("presentation.frag", "debug.frag"),
        points=name == "stars.frag",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslang", default="glslangValidator")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--node", default="node")
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--download-tools",
        action="store_true",
        help="Fetch missing checksum-pinned build tools",
    )
    args = parser.parse_args()
    TOOLS.update({"glslangValidator": args.glslang, "spirv-cross": args.spirv_cross})
    tools = args.tools.resolve()
    tools.mkdir(parents=True, exist_ok=True)
    for name, expected in TWGSL_SHA256.items():
        destination = tools / name
        if not destination.exists() and args.download_tools:
            url = f"https://raw.githubusercontent.com/BabylonJS/Babylon.js/{TWGSL_REVISION}/packages/tools/babylonServer/public/twgsl/{name}"
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"Unexpected downloaded {name}")
            with tempfile.NamedTemporaryFile(dir=tools, delete=False) as temporary:
                temporary.write(data)
                temporary_path = Path(temporary.name)
            os.replace(temporary_path, destination)
        if hashlib.sha256((tools / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Unexpected {name}; use pinned twgsl {TWGSL_REVISION}")
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        manifest = []
        seen_keys = set()
        for i, (label, vertex, fragment) in enumerate(programs()):
            varying_names = []
            for m in DECL.finditer(vertex):
                if m[3] == "out":
                    varying_names.extend(n.strip() for n in m[5].split(","))
            varyings = dict(zip(varying_names, range(len(varying_names))))
            item = {
                "label": label,
                "key": source_key(vertex) + ":" + source_key(fragment),
                "points": label == "stars.frag",
            }
            if item["key"] in seen_keys:
                raise ValueError("Duplicate shader source fingerprint: " + label)
            seen_keys.add(item["key"])
            for stage, original in [("vert", vertex), ("frag", fragment)]:
                converted, samplers = convert_program(label, original, stage, varyings)
                file = directory / f"{i}.{stage}"
                file.write_text(converted)
                spv = directory / f"{i}.{stage}.spv"
                try:
                    run(
                        "glslangValidator",
                        "-V",
                        "-Os",
                        "-S",
                        stage,
                        str(file),
                        "-o",
                        str(spv),
                    )
                    reflected = json.loads(run("spirv-cross", str(spv), "--reflect"))
                except subprocess.CalledProcessError as error:
                    raise RuntimeError(label + "\n" + error.output) from error
                ubo = reflected.get("ubos", [])
                block = ubo[0] if ubo else None
                members = reflected["types"][block["type"]]["members"] if block else []
                logical = logical_uniform_types(original)
                for member in members:
                    if member["name"] in logical:
                        if member["type"] != "vec4" or member.get("array_stride") != 16:
                            raise ValueError(
                                "Unexpected padded array layout: " + member["name"]
                            )
                        member["valueType"] = logical[member["name"]]
                item[stage] = {
                    "path": str(spv),
                    "blockSize": block["block_size"] if block else 0,
                    "members": members,
                    "samplers": [
                        {
                            "name": name,
                            "type": typ,
                            "binding": 2 * n,
                            "sampled": 2 * n + 1
                            in {
                                resource["binding"]
                                for resource in reflected.get("separate_samplers", [])
                            },
                        }
                        for n, (typ, name) in enumerate(samplers)
                        if 2 * n
                        in {
                            resource["binding"]
                            for resource in reflected.get("separate_images", [])
                        }
                    ],
                    "inputs": reflected.get("inputs", []),
                }
            manifest.append(item)
        file = directory / "manifest.json"
        file.write_text(json.dumps(manifest))
        # Load the compiler once, only on the build machine.
        script = directory / "compile.cjs"
        script.write_text(r"""const fs=require('fs');
const list=JSON.parse(fs.readFileSync(process.argv[2]));
require(process.argv[3])(process.argv[4]).then(t=>{
 for(const p of list) for(const s of ['vert','frag']){
  const b=fs.readFileSync(p[s].path);
  let code=t.convertSpirV2WGSL(new Uint32Array(b.buffer,b.byteOffset,b.length/4));
  // The pinned compiler emits a redundant legacy stride attribute for
  // naturally aligned arrays. Non-natural vec2 arrays were padded in GLSL;
  // never strip their original 16-byte stride from a natural 8-byte type.
  code=code.replace(/@stride\(16\) (array<vec4<f32>,)/g,'$1');
  code=code.replace(/@stride\(64\) (array<mat4x4<f32>,)/g,'$1');
  if(code.includes('@stride')) throw new Error(p.label+' '+s+' Unsupported legacy array stride: '+code.match(/@stride[^;\n]*/g).join(' | '));
  if(!code.includes('@'+(s==='vert'?'vertex':'fragment'))) throw new Error(p.label+' WGSL conversion failed');
  p[s].code='diagnostic(off, derivative_uniformity);\n'+code;delete p[s].path;
 }
 const temporary=process.argv[5]+'.tmp-'+process.pid;
 fs.writeFileSync(temporary,JSON.stringify(Object.fromEntries(list.map(p=>[p.key,p]))));
 fs.renameSync(temporary,process.argv[5]);
}).catch(e=>{console.error(e);process.exitCode=1;});
""")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                args.node,
                str(script),
                str(file),
                str(tools / "twgsl.js"),
                str(tools / "twgsl.wasm"),
                str(args.output),
            ],
            check=True,
        )
    print(f"Baked {len(manifest)} shared programs to {args.output}")


if __name__ == "__main__":
    main()
