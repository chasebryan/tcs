"""Validate every authority-bearing field of the seed system description."""
import argparse
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
TERMINAL_DOMAINS = {"terminal": 10, "client": 20, "storage": 30,
                    "policy": 40, "audit": 50, "serial": 60}
TERMINAL_EDGES = {
    ("terminal", "0", "serial", "1"),
    ("terminal", "1", "client", "0"),
    ("client", "1", "storage", "0"),
    ("storage", "1", "policy", "1"),
    ("policy", "2", "audit", "0"),
}


def require(condition, message):
    # These checks must remain enabled under python -O.
    if not condition:
        raise ValueError(message)


def attributes(element, required, optional=()):
    require(set(element.attrib) >= set(required), "missing " + element.tag + " attribute")
    require(set(element.attrib) <= set(required) | set(optional),
            "unexpected " + element.tag + " attribute")


def validate(path, profile="seed"):
    require(profile in {"seed", "terminal"}, "unknown system profile")
    terminal = profile == "terminal"
    domains = TERMINAL_DOMAINS if terminal else DOMAINS
    expected_edges = TERMINAL_EDGES if terminal else EDGES
    root = ET.parse(path).getroot()
    require(root.tag == "system" and not root.attrib, "unexpected system root")
    allowed = {"protection_domain", "channel"} | ({"memory_region"} if terminal else set())
    require(all(child.tag in allowed for child in root),
            "seed forbids extra resources or top-level elements")
    for element in root.iter():
        require(not (element.text or "").strip() and not (element.tail or "").strip(),
                "unexpected text in system description")
    if terminal:
        regions = root.findall("memory_region")
        require(len(regions) == 1 and not list(regions[0]) and regions[0].attrib == {
            "name": "uart", "size": "0x1000", "phys_addr": "0x09000000"},
            "terminal permits only the fixed UART page")
    priorities = {}
    for pd in root.findall("protection_domain"):
        attributes(pd, {"name", "priority", "budget", "period"})
        name = pd.attrib["name"]
        require(name in domains and name not in priorities, "unexpected or duplicate domain")
        priority = int(pd.attrib["priority"])
        require(priority == domains[name], "unexpected profile priority")
        priorities[name] = priority
        budget = 2000 if terminal and name == "serial" else 10000
        require(int(pd.attrib["budget"]) == budget and int(pd.attrib["period"]) == 10000,
                "unexpected seed scheduling budget")
        serial = terminal and name == "serial"
        require(len(pd) == (3 if serial else 1) and pd[0].tag == "program_image",
                "seed forbids device maps, interrupts, children, and extra domain elements")
        image = pd[0]
        attributes(image, {"path"})
        require(not list(image) and image.attrib["path"] == name + ".elf",
                "unexpected program image")
        if serial:
            require(pd[1].tag == "map" and not list(pd[1]) and pd[1].attrib == {
                "mr": "uart", "vaddr": "0x4000000", "perms": "rw", "cached": "false",
                "setvar_vaddr": "uart_base_vaddr"}, "unexpected serial device mapping")
            require(pd[2].tag == "irq" and not list(pd[2]) and pd[2].attrib == {
                "irq": "33", "id": "0", "trigger": "level"}, "unexpected serial IRQ")
    require(priorities == domains, "missing profile domain")
    seen_slots = {("serial", "0")} if terminal else set()
    edges = set()
    for channel in root.findall("channel"):
        require(not channel.attrib, "unexpected channel attribute")
        ends = list(channel)
        require(len(ends) == 2 and all(end.tag == "end" for end in ends),
                "channel must have exactly two ends")
        for end in ends:
            attributes(end, {"pd", "id", "notify"}, {"pp"})
            require(not list(end), "unexpected channel end child")
            require(end.attrib["pd"] in domains, "unknown channel domain")
            require(end.attrib.get("pp", "false") in {"true", "false"}, "invalid pp boolean")
            slot = (end.attrib["pd"], end.attrib["id"])
            notify = "true" if terminal and slot == ("serial", "1") else "false"
            require(end.attrib["notify"] == notify, "unused notification authority forbidden")
            require(slot not in seen_slots, "channel slot reused")
            seen_slots.add(slot)
        callers = [end for end in ends if end.attrib.get("pp") == "true"]
        require(len(callers) == 1, "seed permits one-way RPC only")
        caller = callers[0]
        callee = ends[1] if caller is ends[0] else ends[0]
        a, b = caller.attrib["pd"], callee.attrib["pd"]
        require(priorities[a] < priorities[b], "RPC cycle or priority inversion")
        edges.add((a, caller.attrib["id"], b, callee.attrib["id"]))
    require(edges == expected_edges, "unexpected authority path or channel identity")
    print("PASS exact", profile, "resources, images, RPC identities, priorities, and notifications")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    parser.add_argument("--profile", choices=("seed", "terminal"), default="seed")
    args = parser.parse_args()
    try:
        validate(args.path, args.profile)
    except (ValueError, ET.ParseError) as error:
        raise SystemExit("Invalid TCS system: " + str(error)) from error
