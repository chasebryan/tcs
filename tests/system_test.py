"""Negative authority-topology tests, including Python's optimized mode."""
import contextlib
import copy
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_system", ROOT / "tools/check_system.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class SystemTest(unittest.TestCase):
    def setUp(self):
        self.original = ET.parse(ROOT / "system/tcs.system").getroot()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "test.system"

    def write(self, root):
        ET.ElementTree(root).write(self.path, encoding="unicode")

    def test_valid_seed(self):
        with contextlib.redirect_stdout(io.StringIO()):
            checker.validate(ROOT / "system/tcs.system")

    def test_invalid_authority_changes(self):
        def extra_memory(root):
            ET.SubElement(root, "memory_region", name="unexpected", size="0x1000")

        def device_map(root):
            ET.SubElement(root[0], "map", mr="unexpected", vaddr="0x4000000", perms="rw")

        def interrupt(root):
            ET.SubElement(root[0], "irq", irq="33", id="4")

        def nested_domain(root):
            ET.SubElement(root[0], "protection_domain", name="child")

        changes = {
            "extra physical authority": extra_memory,
            "device map": device_map,
            "interrupt": interrupt,
            "child domain": nested_domain,
            "wrong executable": lambda r: r[0][0].set("path", "../other.elf"),
            "unknown executable attribute": lambda r: r[0][0].set("extra", "true"),
            "unused notification": lambda r: r.find("channel/end").set("notify", "true"),
            "implicit notification": lambda r: r.find("channel/end").attrib.pop("notify"),
            "unknown end attribute": lambda r: r.find("channel/end").set("extra", "true"),
            "wrong identity": lambda r: r.find("channel/end").set("id", "7"),
            "unknown target": lambda r: r.find("channel/end").set("pd", "stranger"),
            "duplicate domain": lambda r: r.append(copy.deepcopy(r[0])),
            "duplicate channel": lambda r: r.append(copy.deepcopy(r.find("channel"))),
            "priority change": lambda r: r[0].set("priority", "60"),
            "budget change": lambda r: r[0].set("budget", "20000"),
            "ambient privilege": lambda r: r[0].set("smc", "true"),
            "missing domain": lambda r: r.remove(r[0]),
            "missing edge": lambda r: r.remove(r.find("channel")),
            "unknown root attribute": lambda r: r.set("extra", "true"),
            "invalid pp boolean": lambda r: r.find("channel/end").set("pp", "yes"),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                root = copy.deepcopy(self.original)
                change(root)
                self.write(root)
                with self.assertRaises(ValueError):
                    checker.validate(self.path)

    def test_both_directions_cannot_call(self):
        root = copy.deepcopy(self.original)
        root.find("channel")[1].set("pp", "true")
        self.write(root)
        with self.assertRaisesRegex(ValueError, "one-way"):
            checker.validate(self.path)

    def test_optimized_python_still_rejects(self):
        root = copy.deepcopy(self.original)
        root.find("channel/end").set("notify", "true")
        self.write(root)
        result = subprocess.run([sys.executable, "-O", str(ROOT / "tools/check_system.py"),
                                 str(self.path)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("notification authority forbidden", result.stderr)

    def test_terminal_profile_and_mutations(self):
        original = ET.parse(ROOT / "system/terminal.system").getroot()
        with contextlib.redirect_stdout(io.StringIO()):
            checker.validate(ROOT / "system/terminal.system", "terminal")

        def serial(root):
            return root.find("protection_domain[@name='serial']")

        def admin_route(root):
            channel = ET.SubElement(root, "channel")
            ET.SubElement(channel, "end", pd="terminal", id="2", pp="true", notify="false")
            ET.SubElement(channel, "end", pd="policy", id="0", notify="false")

        changes = {
            "admin route": admin_route,
            "extra physical page": lambda r: r[0].set("size", "0x2000"),
            "different device": lambda r: r[0].set("phys_addr", "0x9010000"),
            "executable MMIO": lambda r: serial(r).find("map").set("perms", "rwx"),
            "cached device": lambda r: serial(r).find("map").set("cached", "true"),
            "different mapping": lambda r: serial(r).find("map").set("vaddr", "0x5000000"),
            "different IRQ": lambda r: serial(r).find("irq").set("irq", "34"),
            "IRQ channel collision": lambda r: serial(r).find("irq").set("id", "1"),
            "edge IRQ": lambda r: serial(r).find("irq").set("trigger", "edge"),
            "serial CPU budget": lambda r: serial(r).set("budget", "10000"),
            "terminal device access": lambda r: r[1].append(copy.deepcopy(serial(r).find("map"))),
            "terminal sends notification": lambda r: r.find("channel/end").set("notify", "true"),
            "serial calls terminal": lambda r: r.find("channel")[1].set("pp", "true"),
            "missing serial wakeup": lambda r: r.find("channel")[1].set("notify", "false"),
            "missing device": lambda r: r.remove(r[0]),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                root = copy.deepcopy(original)
                change(root)
                self.write(root)
                with self.assertRaises(ValueError):
                    checker.validate(self.path, "terminal")


if __name__ == "__main__":
    unittest.main()
