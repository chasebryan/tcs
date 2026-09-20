"""Validate every authority-bearing field of the seed system description."""
import sys
import xml.etree.ElementTree as ET

DOMAINS = {"console": 10, "client": 20, "storage": 30, "policy": 40, "audit": 50}
EDGES = {
    ("console", "0", "policy", "0"),
    ("console", "1", "client", "0"),
    ("client", "1", "storage", "0"),
    ("storage", "1", "policy", "1"),
    ("policy", "2", "audit", "0"),
    ("console", "2", "audit", "1"),
}


def require(condition, message):
    # These checks must remain enabled under python -O.
    if not condition:
        raise ValueError(message)


def attributes(element, required, optional=()):
    require(set(element.attrib) >= set(required), "missing " + element.tag + " attribute")
    require(set(element.attrib) <= set(required) | set(optional),
            "unexpected " + element.tag + " attribute")


def validate(path):
    root = ET.parse(path).getroot()
    require(root.tag == "system" and not root.attrib, "unexpected system root")
    require(all(child.tag in {"protection_domain", "channel"} for child in root),
            "seed forbids extra resources or top-level elements")
    for element in root.iter():
        require(not (element.text or "").strip() and not (element.tail or "").strip(),
                "unexpected text in system description")
    priorities = {}
    for pd in root.findall("protection_domain"):
        attributes(pd, {"name", "priority", "budget", "period"})
        name = pd.attrib["name"]
        require(name in DOMAINS and name not in priorities, "unexpected or duplicate domain")
        priority = int(pd.attrib["priority"])
        require(priority == DOMAINS[name], "unexpected seed priority")
        priorities[name] = priority
        require(int(pd.attrib["budget"]) == 10000 and int(pd.attrib["period"]) == 10000,
                "unexpected seed scheduling budget")
        require(len(pd) == 1 and pd[0].tag == "program_image",
                "seed forbids device maps, interrupts, children, and extra domain elements")
        image = pd[0]
        attributes(image, {"path"})
        require(not list(image) and image.attrib["path"] == name + ".elf",
                "unexpected program image")
    require(priorities == DOMAINS, "missing seed domain")
    seen_slots, edges = set(), set()
    for channel in root.findall("channel"):
        require(not channel.attrib, "unexpected channel attribute")
        ends = list(channel)
        require(len(ends) == 2 and all(end.tag == "end" for end in ends),
                "channel must have exactly two ends")
        for end in ends:
            attributes(end, {"pd", "id", "notify"}, {"pp"})
            require(not list(end), "unexpected channel end child")
            require(end.attrib["pd"] in DOMAINS, "unknown channel domain")
            require(end.attrib.get("pp", "false") in {"true", "false"}, "invalid pp boolean")
            require(end.attrib["notify"] == "false", "unused notification authority forbidden")
            slot = (end.attrib["pd"], end.attrib["id"])
            require(slot not in seen_slots, "channel slot reused")
            seen_slots.add(slot)
        callers = [end for end in ends if end.attrib.get("pp") == "true"]
        require(len(callers) == 1, "seed permits one-way RPC only")
        caller = callers[0]
        callee = ends[1] if caller is ends[0] else ends[0]
        a, b = caller.attrib["pd"], callee.attrib["pd"]
        require(priorities[a] < priorities[b], "RPC cycle or priority inversion")
        edges.add((a, caller.attrib["id"], b, callee.attrib["id"]))
    require(edges == EDGES, "unexpected authority path or channel identity")
    print("PASS exact seed resources, images, RPC identities, priorities, and no notifications")


if __name__ == "__main__":
    try:
        validate(sys.argv[1])
    except (ValueError, ET.ParseError) as error:
        raise SystemExit("Invalid TCS system: " + str(error)) from error
