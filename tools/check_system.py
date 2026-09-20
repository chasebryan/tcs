"""Check the Seed's explicit communication graph before image construction."""
import sys
import xml.etree.ElementTree as ET


def validate(path):
    root = ET.parse(path).getroot()
    priorities = {}
    for pd in root.findall("protection_domain"):
        name = pd.attrib["name"]
        assert name not in priorities, "duplicate domain"
        priorities[name] = int(pd.attrib["priority"])
        assert 0 < int(pd.attrib["budget"]) <= int(pd.attrib["period"])
    expected = {
        ("console", "0", "policy", "0"),
        ("console", "1", "client", "0"),
        ("client", "1", "storage", "0"),
        ("storage", "1", "policy", "1"),
        ("policy", "2", "audit", "0"),
        ("console", "2", "audit", "1"),
    }
    seen_slots, edges = set(), set()
    for channel in root.findall("channel"):
        ends = channel.findall("end")
        assert len(ends) == 2
        for end in ends:
            slot = (end.attrib["pd"], end.attrib["id"])
            assert slot not in seen_slots, "channel slot reused"
            seen_slots.add(slot)
        callers = [end for end in ends if end.attrib.get("pp") == "true"]
        assert len(callers) == 1, "Seed permits one-way RPC only"
        caller = callers[0]
        callee = ends[1] if caller is ends[0] else ends[0]
        a, b = caller.attrib["pd"], callee.attrib["pd"]
        assert priorities[a] < priorities[b], "RPC cycle or priority inversion"
        edges.add((a, caller.attrib["id"], b, callee.attrib["id"]))
    assert edges == expected, "unexpected authority path or channel identity"
    assert set(priorities) == {"console", "client", "storage", "policy", "audit"}
    print("PASS five-domain graph, channel identity bindings, and acyclic RPC priorities")


if __name__ == "__main__":
    validate(sys.argv[1])
