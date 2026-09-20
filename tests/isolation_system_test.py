"""Test-only authority must remain exact and forbidden in normal images."""
import contextlib
import copy
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_system import validate

ROOT = Path(__file__).resolve().parents[1]


class IsolationSystemTests(unittest.TestCase):
    def test_test_profile_not_accepted_as_normal(self):
        with contextlib.redirect_stdout(io.StringIO()):
            validate(ROOT / "system/isolation.system", "isolation")
        for profile in ("seed", "terminal"):
            with self.assertRaises(ValueError):
                validate(ROOT / "system/isolation.system", profile)

    def test_mutations_fail(self):
        def terminal(r): return r.find("protection_domain[@name='terminal']")
        def policy(r): return r.find("protection_domain[@name='policy']")
        def probe(r, n): return terminal(r).find(f"protection_domain[@id='{n}']")
        def channel(r):
            edge = ET.SubElement(r, "channel")
            ET.SubElement(edge, "end", pd="probe1", id="0", pp="true", notify="false")
            ET.SubElement(edge, "end", pd="policy", id="0", notify="false")
        changes = {
            "probe admin route": channel,
            "unknown attribute": lambda r: probe(r, 1).set("smc", "true"),
            "probe device mapping": lambda r: probe(r, 1).append(copy.deepcopy(r.find("protection_domain[@name='serial']/map"))),
            "probe IRQ": lambda r: ET.SubElement(probe(r, 1), "irq", irq="33", id="0"),
            "probe extra child": lambda r: probe(r, 1).append(copy.deepcopy(probe(r, 2))),
            "duplicate child": lambda r: terminal(r).append(copy.deepcopy(probe(r, 1))),
            "missing child": lambda r: terminal(r).remove(probe(r, 3)),
            "wrong child image": lambda r: probe(r, 1)[0].set("path", "terminal.elf"),
            "high-priority child": lambda r: probe(r, 1).set("priority", "11"),
            "different budget": lambda r: probe(r, 1).set("budget", "9999"),
            "readonly becomes writable": lambda r: probe(r, 5).find("map").set("perms", "rw"),
            "NX becomes executable": lambda r: probe(r, 6).find("map").set("perms", "rwx"),
            "observer writes canary": lambda r: terminal(r).find("map").set("perms", "rw"),
            "policy cannot initialize canary": lambda r: policy(r).find("map").set("perms", "r"),
            "policy canary moved": lambda r: policy(r).find("map").set("vaddr", "0x5100000"),
            "different memory size": lambda r: r[1].set("size", "0x2000"),
            "extra physical authority": lambda r: r[1].set("phys_addr", "0x9000000"),
            "duplicate canary region": lambda r: r.append(copy.deepcopy(r[1])),
            "unexpected text": lambda r: setattr(probe(r, 1), "text", "unexpected"),
            "observer image swap": lambda r: terminal(r)[0].set("path", "terminal.elf"),
            "policy image swap": lambda r: policy(r)[0].set("path", "policy.elf"),
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.system"
            for name, change in changes.items():
                with self.subTest(name=name):
                    root = ET.parse(ROOT / "system/isolation.system").getroot()
                    change(root)
                    ET.ElementTree(root).write(path)
                    with self.assertRaises(ValueError):
                        validate(path, "isolation")
            result = subprocess.run([sys.executable, "-O", str(ROOT / "tools/check_system.py"), str(path), "--profile", "isolation"], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unexpected isolation image", result.stderr)


if __name__ == "__main__":
    unittest.main()
