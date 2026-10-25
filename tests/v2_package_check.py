"""Check the release payload without compiling audio/UI binaries."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / ('tools/' + name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value

pack = module('package_v2_test')
publish = module('publish_v2_alpha')


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for name in ('CMakeLists.txt', 'LICENSE', 'README.md', 'docs/V2_TEST_BUILD.md', 'juce/LICENSE.md',
                     'content/factory/catalogue.json', 'build/CyDAW 2.exe', 'build/CyDAWPluginWorker.exe',
                     'build/fonts/Roboto-Medium.ttf', 'build/fonts/Cousine-Regular.ttf',
                     'build/i18n/en.json', 'build/Licenses/FFmpeg/LICENSE-LGPL-3.0.txt',
                     'build/Licenses/FFmpeg/sources/ffmpeg.tar.gz', 'build/ControlPlugins/Gamepads.cyctl'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('project(CyDAW VERSION 0.77.0)' if name == 'CMakeLists.txt' else 'fixture', 'utf-8')
        for component in ('avcodec', 'avformat', 'avutil', 'swscale', 'swresample'):
            (self.root / 'build' / (component + '-1.dll')).write_bytes(b'fixture')
        self.patch = patch.object(pack, 'ROOT', self.root)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.temp.cleanup()

    def test_complete_alpha_and_checksums(self):
        archive = pack.package(self.root / 'build', self.root / 'out', True, self.root / 'juce')
        with zipfile.ZipFile(archive) as bundle:
            names = bundle.namelist()
            self.assertTrue(any(n.endswith('/ALPHA.txt') for n in names))
            self.assertTrue(any(n.endswith('/avcodec-1.dll') for n in names))
            self.assertTrue(any(n.endswith('/Licenses/FFmpeg/sources/ffmpeg.tar.gz') for n in names))
            self.assertTrue(any(n.endswith('/ControlPlugins/Gamepads.cyctl') for n in names))
        self.assertEqual(len(publish.verified_assets(self.root / 'out')), 3)
        archive.write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            publish.verified_assets(self.root / 'out')

    def test_missing_codec_fails_before_packaging(self):
        (self.root / 'build/avcodec-1.dll').unlink()
        with self.assertRaises(FileNotFoundError):
            pack.package(self.root / 'build', self.root / 'out', True, self.root / 'juce')

    def test_only_versioned_alpha_tags(self):
        self.assertEqual(publish.check_tag('v2-0.77.0-alpha.1'), '0.77.0')
        for tag in ('v0.77.0', 'v2-0.77.0', 'v2-0.78.0-alpha.1', 'v2-0.77.0-alpha.0', '../alpha'):
            with self.assertRaises(ValueError):
                publish.check_tag(tag)


if __name__ == '__main__':
    unittest.main()
