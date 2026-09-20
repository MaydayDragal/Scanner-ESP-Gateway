"""SDK-free tests of the narrow local lock transformation."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from rebase_dependencies_lock import rebase_local_source


class LockRebaseTests(unittest.TestCase):
    def setUp(self):
        self.lock = {
            "dependencies": {
                "espressif/esp_tinyusb": {
                    "version": "2.3.0",
                    "source": {"type": "local", "path": "C:\\old-machine\\project\\components\\esp_tinyusb"},
                    "dependencies": [{"name": "idf", "version": ">=5.0"}],
                },
                "espressif/tinyusb": {"version": "0.21.0~2", "component_hash": "a" * 64, "source": {"type": "service", "registry_url": "https://components.espressif.com"}},
                "espressif/led_strip": {"version": "3.0.3", "component_hash": "b" * 64, "source": {"type": "service", "registry_url": "https://components.espressif.com"}},
            },
            "manifest_hash": "c" * 64,
            "target": "esp32s3", "version": "2.0.0",
        }

    def test_relocates_windows_and_linux_without_changing_any_other_value(self):
        before = deepcopy(self.lock)
        for path in ("D:\\new checkout\\components\\esp_tinyusb", "/home/runner/work/gateway/components/esp_tinyusb"):
            with self.subTest(path=path):
                source = {"type": "local", "path": path}
                expected = deepcopy(self.lock)
                expected["dependencies"]["espressif/esp_tinyusb"]["source"] = source
                self.assertEqual(rebase_local_source(self.lock, source), expected)
                self.assertEqual(self.lock, before)

    def test_unchanged_source_is_noop(self):
        source = self.lock["dependencies"]["espressif/esp_tinyusb"]["source"]
        self.assertEqual(rebase_local_source(self.lock, source), self.lock)

    def test_registry_baseline_stays_unchanged_for_normal_idf_resolution(self):
        self.lock["dependencies"]["espressif/esp_tinyusb"]["source"] = {"type": "service", "registry_url": "https://components.espressif.com"}
        self.assertEqual(rebase_local_source(self.lock, {"type": "local", "path": "/new/components/esp_tinyusb"}), self.lock)

    def test_malformed_inputs_fail_explicitly(self):
        source = {"type": "local", "path": "/new/components/esp_tinyusb"}
        for bad in (None, {}, {"dependencies": []}, {"dependencies": {}}, {"dependencies": {"espressif/esp_tinyusb": {}}}):
            with self.subTest(lock=bad), self.assertRaises(ValueError):
                rebase_local_source(bad, source)
        for bad in ({}, {"type": "service", "path": "/new"}, {"type": "local", "path": "relative"}, {"type": "local", "path": 4}):
            with self.subTest(source=bad), self.assertRaises(ValueError):
                rebase_local_source(self.lock, bad)
        for bad in ({"type": "local"}, {"type": "git", "path": "/old"}, {"type": "local", "path": None}):
            lock = deepcopy(self.lock)
            lock["dependencies"]["espressif/esp_tinyusb"]["source"] = bad
            with self.subTest(old_source=bad), self.assertRaises(ValueError):
                rebase_local_source(lock, source)

    def test_led_override_relocates_without_changing_usb_or_versions(self):
        component = "espressif/led_strip"
        self.lock["dependencies"][component]["source"] = {
            "type": "local", "path": "C:\\removed\\components\\led_strip"}
        before = deepcopy(self.lock)
        for path in ("D:\\new checkout\\components\\led_strip", "/runner/components/led_strip"):
            with self.subTest(path=path):
                source = {"type": "local", "path": path}
                expected = deepcopy(self.lock)
                expected["dependencies"][component]["source"] = source
                self.assertEqual(rebase_local_source(self.lock, source, component), expected)
                self.assertEqual(self.lock, before)

    def test_led_registry_entry_stays_for_normal_resolution(self):
        source = {"type": "local", "path": "/runner/components/led_strip"}
        self.assertEqual(rebase_local_source(self.lock, source, "espressif/led_strip"), self.lock)

    def test_only_reviewed_component_versions_are_rebased(self):
        source = {"type": "local", "path": "/runner/components/led_strip"}
        before = deepcopy(self.lock)
        with self.assertRaises(ValueError):
            rebase_local_source(self.lock, source, "espressif/tinyusb")
        self.assertEqual(self.lock, before)
        self.lock["dependencies"]["espressif/led_strip"]["version"] = "3.0.4"
        before = deepcopy(self.lock)
        with self.assertRaises(ValueError):
            rebase_local_source(self.lock, source, "espressif/led_strip")
        self.assertEqual(self.lock, before)


if __name__ == "__main__":
    unittest.main()
