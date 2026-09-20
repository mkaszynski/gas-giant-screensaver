# Maintainer tool: generate contextual patch from the exact installed upstream patches.
# Paths are passed explicitly; no machine paths are embedded in the generated patch.
import difflib
import pathlib
import shutil
import subprocess
import sys
import tempfile

base = pathlib.Path(sys.argv[1])
patches = [pathlib.Path(p) for p in sys.argv[2:]]
with tempfile.TemporaryDirectory() as temp:
    root = pathlib.Path(temp)
    shutil.copytree(base / 'src', root / 'src')
    for item in (root / 'src').rglob('*'):
        item.chmod(0o755 if item.is_dir() else 0o644)
    (root / 'src').chmod(0o755)
    for patch in patches:
        subprocess.run(['patch', '-p1', '-i', str(patch.resolve())], cwd=root, check=True, stdout=subprocess.DEVNULL)
    p = root / 'src/config/ConfigManager.cpp'
    text = p.read_text()
    a = text.index('    m_config.addSpecialCategory("starfield"')
    b = text.index('    m_config.addSpecialCategory("shape"', a)
    text = text[:a] + '''    m_config.addSpecialCategory("observatory", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("observatory", "monitor", Hyprlang::STRING{""});
    m_config.addSpecialConfigValue("observatory", "fps", Hyprlang::INT{30});
    m_config.addSpecialConfigValue("observatory", "day_seconds", Hyprlang::INT{1800});
    m_config.addSpecialConfigValue("observatory", "zindex", Hyprlang::INT{-1});

''' + text[b:]
    a = text.index('    keys = m_config.listKeysForSpecialCategory("starfield")')
    b = text.index('    keys = m_config.listKeysForSpecialCategory("shape")', a)
    text = text[:a] + '''    keys = m_config.listKeysForSpecialCategory("observatory");
    for (auto& k : keys) {
        result.push_back(CConfigManager::SWidgetConfig{
            .type = "observatory",
            .monitor = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("observatory", "monitor", k.c_str())),
            .values = {
                {"fps", m_config.getSpecialConfigValue("observatory", "fps", k.c_str())},
                {"day_seconds", m_config.getSpecialConfigValue("observatory", "day_seconds", k.c_str())},
                {"zindex", m_config.getSpecialConfigValue("observatory", "zindex", k.c_str())},
            }
        });
    }
''' + text[b:]
    p.write_text(text)
    p = root / 'src/renderer/Renderer.cpp'
    text = p.read_text().replace('widgets/Starfield.hpp', 'widgets/Observatory.hpp').replace('c.type == "starfield"','c.type == "observatory"').replace('createWidget<CStarfield>', 'createWidget<CObservatory>')
    p.write_text(text)
    diff = []
    for path in sorted((root / 'src').rglob('*')):
        rel = path.relative_to(root)
        if not path.is_file() or not (base/rel).is_file():
            continue
        before = (base/rel).read_text()
        after = path.read_text()
        if before != after:
            diff.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),fromfile='a/'+str(rel),tofile='b/'+str(rel)))
    pathlib.Path('hyprlock/patches/integration.patch').write_text(''.join(diff))
