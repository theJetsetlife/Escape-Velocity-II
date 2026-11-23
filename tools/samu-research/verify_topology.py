#!/usr/bin/env python3
"""Check static host-SBL and published SNVS routes against pinned offline evidence.

No hardware I/O, firmware modifications, secret material, or network access.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HOST_NODES = ["x86_orbis_sbl", "sbl_driver", "gpu_apu_indexed_interface", "samu_security_domain"]
SNVS_NODES = ["samu_protected_storage", "apu_auxiliary_services", "aeolia_icc", "syscon_secure_nvs"]
EXPECTED_SHA = "8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3"
REQUIRED_EDGES = {
    ("0xffffffff999f9060", "0xffffffff999e4ed0"),
    ("0xffffffff999e4ed0", "0xffffffff999e4ee0"),
    ("0xffffffff999e4ee0", "0xffffffff999e4a20"),
}


def validate(topology, evidence, call_graph):
    if topology.get("schema_version") != 1 or topology.get("board") != "SAA-001 / Liverpool / Aeolia":
        raise ValueError("Unexpected board or schema version")
    if topology.get("source_kernel_sha256") != EXPECTED_SHA or evidence.get("sha256") != EXPECTED_SHA or call_graph.get("image_sha256") != EXPECTED_SHA:
        raise ValueError("Mismatched source binary provenance")
    if evidence.get("pass_all") is not True or evidence.get("passed") != 19 or evidence.get("program_header_count") != 6:
        raise ValueError("Pinned executable evidence is not verified")

    routes = topology.get("routes", [])
    if len(routes) != 2 or {r.get("id") for r in routes} != {"host_sbl", "snvs_external"}:
        raise ValueError("Two distinct routes are required")
    host = next(r for r in routes if r["id"] == "host_sbl")
    snvs = next(r for r in routes if r["id"] == "snvs_external")
    if host.get("nodes") != HOST_NODES or host.get("evidence_level") != "verified_host_instructions_only":
        raise ValueError("SBL route incorrectly includes external ICC or overstates evidence")
    if snvs.get("nodes") != SNVS_NODES or snvs.get("evidence_level") != "published_external_research_not_user_bus_capture":
        raise ValueError("External SNVS route is missing or overstated")
    sources = snvs.get("references", [])
    if not isinstance(sources, list) or len(sources) < 2 or not all(s.startswith("https://fail0verflow.com/blog/2018/ps4-") for s in sources):
        raise ValueError("SNVS route requires primary research citations")

    pinned = host.get("pinned_instruction_checks")
    if not isinstance(pinned, dict) or len(pinned) < 10:
        raise ValueError("Missing pinned 9.00 instruction evidence")
    actual = evidence.get("checks", {})
    for name, expected_bytes in pinned.items():
        found = actual.get(name, {})
        if found.get("verified") is not True or found.get("actual_bytes") != expected_bytes:
            raise ValueError(f"Host instruction anchor is unverified or mismatched: {name}")

    advertised = {tuple(edge) for edge in host.get("verified_edges", [])}
    verified = {(e.get("from"), e.get("to")) for e in call_graph.get("edges", []) if e.get("confidence") == "verified"}
    if not REQUIRED_EDGES.issubset(advertised) or not advertised.issubset(verified):
        raise ValueError("Host route cites a missing or inferred edge as verified")
    if any("aeolia" in x or "syscon" in x or "icc" in x for x in HOST_NODES):
        raise ValueError("Unexpected external controller in host route")
    if not topology.get("guardrails"):
        raise ValueError("Missing research safety boundaries")
    return {"board": topology["board"], "routes": 2,
            "host_checks": len(pinned), "verified_edges": len(advertised),
            "hardware_transactions": 0}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--topology", type=Path, default=ROOT / "transport_topology.json")
    p.add_argument("--evidence", type=Path, default=ROOT / "evidence.json")
    p.add_argument("--graph", type=Path, default=ROOT / "call_graph.json")
    args = p.parse_args()
    data = [json.loads(x.read_text(encoding="utf-8")) for x in (args.topology, args.evidence, args.graph)]
    print(json.dumps(validate(*data), indent=2))


if __name__ == "__main__":
    main()
