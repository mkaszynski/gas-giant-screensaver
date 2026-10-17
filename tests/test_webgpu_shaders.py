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
        self.assertEqual(len(programs), 17)
        self.assertEqual(
            len(
                {bake.source_key(v) + ":" + bake.source_key(f) for _, v, f in programs}
            ),
            17,
        )
        for label, vertex, fragment in programs:
            self.assertNotIn("#include", vertex + fragment, label)
            self.assertIn("void main()", fragment, label)
        self.assertIn("SURFACE_REFLECTION", programs[-2][2])

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

    def test_unbalanced_input_is_rejected(self):
        with self.assertRaises(ValueError):
            bake.closing_paren("call(nested(1.)", 4)


if __name__ == "__main__":
    unittest.main()
