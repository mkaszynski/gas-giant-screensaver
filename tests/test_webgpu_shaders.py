#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "webgpu_shaders", ROOT / "scripts/build_webgpu_shaders.py"
)
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


class ShaderBakeTests(unittest.TestCase):
    def test_source_identity_matches_browser(self):
        fixtures = [
            "void main(){color=1.;}",
            " // ignored\n void main() { /* also ignored */ color=1.;}",
            "#define TEST\nvoid main(){color=2.;}",
            "// 🌌\r\nvoid main(){}",
        ]
        script = "const {sourceKey}=require('./web/graphics-data.js');console.log(JSON.stringify(JSON.parse(process.argv[1]).map(sourceKey)))"
        actual = json.loads(
            subprocess.check_output(
                ["node", "-e", script, json.dumps(fixtures)], cwd=ROOT, text=True
            )
        )
        self.assertEqual(actual, [bake.source_key(text) for text in fixtures])
        self.assertNotEqual(actual[0], actual[1])
        self.assertNotEqual(actual[0], actual[2])

    def test_all_shared_program_variants_are_distinct(self):
        programs = list(bake.programs())
        self.assertEqual(len(programs), 29)
        self.assertEqual(
            len(
                {bake.source_key(v) + ":" + bake.source_key(f) for _, v, f in programs}
            ),
            29,
        )
        self.assertIn("weather-rain.frag", {p[0] for p in programs})
        for label, vertex, fragment in programs:
            self.assertNotIn("#include", vertex + fragment, label)
            self.assertIn("void main()", fragment, label)
        self.assertEqual(
            sum("SURFACE_REFLECTION" in label for label, _, _ in programs), 2
        )

    def test_sky_only_weather_keeps_the_shared_solver_and_constant_pass(self):
        label, vertex, fragment = next(
            p for p in bake.programs()
            if p[0] == "weather.frag:OBSERVATORY_WEATHER_SKY_ONLY"
        )
        self.assertEqual(vertex, bake.source("fullscreen.vert"))
        expected = bake.source("weather.frag").replace(
            "\n", "\n#define OBSERVATORY_WEATHER_SKY_ONLY\n", 1
        )
        self.assertEqual(fragment, expected)
        converted, _ = bake.convert_program(label, fragment, "frag", {"uv": 0})
        compact = "".join(converted.split())
        self.assertIn("constbooluWeatherSkyPass=true;", compact)
        self.assertNotIn("uintuWeatherSkyPass;", compact)
        self.assertIn("integrateWeather", converted)

    def test_debug_overlay_uses_shared_shaders_and_swapchain_coordinates(self):
        label, vertex, fragment = next(p for p in bake.programs() if p[0] == "debug.frag")
        self.assertEqual(vertex, bake.source("debug.vert"))
        self.assertEqual(fragment, bake.source("debug.frag"))
        converted, _ = bake.convert_program(label, vertex, "vert", {"tint": 0})
        self.assertNotIn("gl_Position.y=-gl_Position.y", converted)

    def test_surface_materials_cover_both_cloud_profiles(self):
        programs = [p for p in bake.programs() if p[0].startswith("surface.frag:")]
        self.assertEqual(len(programs), 8)
        for mode in (0, 1):
            for material in (
                "",
                "SURFACE_REFLECTION",
                "SURFACE_DEEP_WATER",
                "SURFACE_PURE_LAND",
            ):
                prefix = f"#define {material}\n" if material else ""
                prefix += f"#define OBSERVATORY_WEATHER_MODE {mode}\n"
                matches = [
                    p for p in programs if p[2].split("\n", 1)[1].startswith(prefix)
                ]
                self.assertEqual(len(matches), 1, (mode, material))
        shadow = next(
            p for p in bake.programs() if p[0].startswith("surface_shadow.frag:")
        )
        self.assertTrue(
            shadow[2]
            .split("\n", 1)[1]
            .startswith("#define OBSERVATORY_WEATHER_MODE 1\n")
        )

    def test_post_variants_keep_scene_target_coordinates(self):
        programs = [p for p in bake.programs() if p[0].startswith("post.frag:")]
        self.assertEqual(len(programs), 2)
        for label, vertex, fragment in programs:
            vert, _ = bake.convert_program(label, vertex, "vert", {"uv": 0})
            frag, _ = bake.convert_program(label, fragment, "frag", {"uv": 0})
            self.assertIn("gl_Position.y=-gl_Position.y", vert)
            self.assertNotIn("uResolution.y-gl_FragCoord.y", frag)
            self.assertIn("weatherPixel", frag)

    def test_presentation_uses_shared_shader_and_swapchain_coordinates(self):
        label, vertex, fragment = next(p for p in bake.programs() if p[0] == "presentation.frag")
        self.assertEqual(fragment, bake.source("presentation.frag"))
        vert, _ = bake.convert_program(label, vertex, "vert", {"uv": 0})
        self.assertNotIn("gl_Position.y=-gl_Position.y", vert)

    def test_meteor_shader_uses_shared_optical_kernel_and_hdr_scaling(self):
        _, vertex, fragment = next(
            program for program in bake.programs() if program[0] == "meteors.frag"
        )
        self.assertEqual(vertex, bake.source("meteors.vert"))
        self.assertIn("float crossPixel(", fragment)
        self.assertIn("flat in vec4 endpoints,light;", fragment)
        self.assertIn("uniform highp float uSceneScale;", fragment)
        self.assertIn("color.rgb*uSceneScale", fragment)

    def test_sampler_function_calls_preserve_nested_arguments(self):
        source = """vec4 read(highp sampler2D image, vec2 point){return texture(image,point);}
vec4 nested(highp sampler2D image, vec2 point){return read(image,point*vec2(2.,3.));}
void main(){color=nested(uImage,vec2(sin(1.),cos(2.)));}"""
        actual = bake.split_sampler_parameters(source)
        self.assertIn(
            "read(texture2D image_texture,sampler image_sampler,vec2 point)", actual
        )
        self.assertIn("texture(sampler2D(image_texture,image_sampler),point)", actual)
        self.assertIn("read(image_texture,image_sampler,point*vec2(2.,3.))", actual)
        self.assertIn(
            "nested(uImage_texture,uImage_sampler,vec2(sin(1.),cos(2.)))", actual
        )

    def test_shadow_samplers_remain_comparison_samplers(self):
        actual = bake.split_sampler_parameters(
            "float sampleDepth(highp sampler2DShadow tex,vec3 uv){return texture(tex,uv);}\nvoid main(){float x=sampleDepth(uShadow,vec3(1.));}"
        )
        self.assertIn("texture2D tex_texture,samplerShadow tex_sampler", actual)
        self.assertIn("sampler2DShadow(tex_texture,tex_sampler)", actual)
        self.assertIn("sampleDepth(uShadow_texture,uShadow_sampler,vec3(1.))", actual)

    def test_padded_uniform_array_keeps_indexed_vector_reads(self):
        original = "uniform highp vec2 offsets[5];"
        source = "vec2 a=offsets[indices[2]];float b=offsets[1].x;"
        actual, uniforms = bake.pad_uniform_arrays(source, [("vec2", "offsets[5]")])
        self.assertEqual(uniforms, [("vec4", "offsets[5]")])
        self.assertEqual(
            actual, "vec2 a=offsets[indices[2]].xy;float b=offsets[1].xy.x;"
        )
        self.assertEqual(bake.logical_uniform_types(original), {"offsets": "vec2"})
        actual, samplers = bake.convert(
            "#version 300 es\n"
            + original
            + "\nlayout(location=0)out vec4 color;void main(){color=vec4(offsets[2],0.,1.);}",
            "frag",
            {},
        )
        self.assertIn("vec4 offsets[5];", actual)
        self.assertIn("offsets[2].xy", actual)
        self.assertEqual(samplers, [])

    def test_unsupported_uniform_array_uses_fail_explicitly(self):
        for source in ("consume(offsets);", "consume(offsets[indices[2]);"):
            with self.assertRaises(ValueError):
                bake.pad_uniform_arrays(source, [("vec2", "offsets[5]")])
        original = "vec4 a=values[2];mat4 b=matrices[0];"
        uniforms = [("vec4", "values[4]"), ("mat4", "matrices[4]")]
        self.assertEqual(
            bake.pad_uniform_arrays(original, uniforms), (original, uniforms)
        )

    def test_unbalanced_input_is_rejected(self):
        with self.assertRaises(ValueError):
            bake.closing_paren("call(nested(1.)", 4)


if __name__ == "__main__":
    unittest.main()
