"""Valid fresh heap JSON dictionaries for host-CPython Hypothesis tests."""

from hypothesis import strategies as st


@st.composite
def valid_heaps(draw, max_objects=20, collection="full"):
    """Construct IDs and bounded references together; never filter invalid heaps."""
    if max_objects < 0:
        raise ValueError("max_objects must be nonnegative")
    if collection not in ("full", "young"):
        raise ValueError("collection must be full or young")
    count = draw(st.integers(min_value=0, max_value=max_objects))
    identities = draw(st.permutations(range(count)))
    reference = st.none() if count == 0 else st.one_of(st.none(), st.integers(0, count - 1))
    immediate = st.one_of(st.sampled_from([0, -(2**63), 2**63 - 1]),
                          st.integers(-(2**63), 2**63 - 1))
    cell = st.one_of(st.builds(lambda value: {"imm": value}, immediate),
                     st.builds(lambda value: {"ref": value}, reference))
    field = st.fixed_dictionaries({"kind": st.just("field"), "value": cell})
    entry = st.fixed_dictionaries({"kind": st.just("entry"),
                                   "guards": st.tuples(reference, reference).map(list),
                                   "payload": cell})
    objects = []
    for allocation in range(count):
        kind = draw(st.sampled_from(["young_original", "old_original", "nonmoving_original"]))
        if kind == "nonmoving_original":
            state = {"kind": kind, "marked": False}
        else:
            state = {"kind": kind, "copy_to": draw(st.sampled_from(["ordinary", "nonmoving"])),
                     "header": {"kind": "normal", "marked": False}}
        objects.append({"object_id": f"obj-{identities[allocation]}", "allocation_id": allocation,
                        "state": state, "strong": draw(st.lists(cell, max_size=4)),
                        "weak": draw(st.lists(field, max_size=2)) + draw(st.lists(entry, max_size=2))})
    external = draw(st.lists(st.one_of(field, entry), max_size=4))
    units = [unit for obj in objects for unit in obj["weak"]] + external
    for unit_id, unit in enumerate(units):
        unit["unit_id"] = unit_id
    old = [o["allocation_id"] for o in objects if o["state"]["kind"] != "young_original"]
    remembered = {owner for owner in old if any(
        "ref" in cell and cell["ref"] is not None and objects[cell["ref"]]["state"]["kind"] == "young_original"
        for cell in objects[owner]["strong"])}
    for owner in old:
        for unit in objects[owner]["weak"]:
            refs = (unit.get("guards", []) +
                    [unit.get("payload", unit.get("value", {})).get("ref")])
            if any(ref is not None and objects[ref]["state"]["kind"] == "young_original"
                   for ref in refs):
                remembered.add(owner)
    extra = draw(st.sets(st.sampled_from(old))) if old else set()
    remembered = list(draw(st.permutations(sorted(remembered | extra))))
    return {"format": "clover-gc-heap", "version": 2,
            "roots": draw(st.lists(cell, max_size=4)),
            "objects": list(draw(st.permutations(objects))), "external_weak": external,
            "remembered": remembered}
