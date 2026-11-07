import copy
import os
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from benchmarks.tools.common import matched_input_authority as authority


class ConsumedProductAuthorityTests(unittest.TestCase):
    def test_authorized_report_refreeze_binds_both_consumed_sides(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            def artifact(name):
                path = root / name
                path.write_text(name)
                return {"path": str(path), "sha256": "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()}

            suffix = ".exe" if os.name == "nt" else ""
            module_name = "clipper2next.dll" if os.name == "nt" else "libclipper2next.so.6.1.1"
            old_module = artifact(module_name)
            new_module = artifact("current-runtime")
            original = [old_module]
            frozen = {"original": {}, "trial": {}}
            for index in range(8):
                name = "product" + str(index)
                original.append(artifact(name + suffix))
                for side in frozen:
                    frozen[side][name] = {
                        "executable": artifact(side + "-" + name + suffix),
                        "runtime": old_module if side == "original" else new_module,
                        "payload_objects": [artifact(side + "-" + name + ".obj")],
                    }
            framework = {name: artifact(name) for name in ("original_runner_source", "patched_runner_source", "original_runner_object", "patched_runner_object", "original_library", "patched_library")}
            payload = {
                "kind": "verified-report-preallocation-product-producer-freeze",
                "baseline": authority.BASELINE_HEAD,
                "platforms": [{"platform": "windows" if os.name == "nt" else "native", "products": frozen, "framework": framework}],
            }
            proof_path = root / "producer-freeze.json"
            proof_path.write_text(json.dumps(payload))
            proof = {"path": str(proof_path), "sha256": "sha256:" + hashlib.sha256(proof_path.read_bytes()).hexdigest()}
            manifest = {
                "product_producer_freeze": proof,
                "old": {"module": old_module, "products": {name: product["executable"] for name, product in frozen["original"].items()}},
                "candidate": {"module": new_module, "core": {"path": str(root / "core")}, "products": {name: product["executable"] for name, product in frozen["trial"].items()}},
            }
            with patch.object(authority, "PRODUCT_PRODUCER_FREEZE_SHA", proof["sha256"].removeprefix("sha256:"), create=True):
                authority.consumed_product_bindings(original, manifest)
                for mutation in (
                    lambda m: m["old"]["products"].update(product0=original[1]),
                    lambda m: m["candidate"]["products"].update(product0=m["old"]["products"]["product0"]),
                    lambda m: m["candidate"].update(module=old_module),
                    lambda m: m["product_producer_freeze"].update(sha256="sha256:" + "0" * 64),
                ):
                    altered = copy.deepcopy(manifest)
                    mutation(altered)
                    with self.assertRaises(ValueError):
                        authority.consumed_product_bindings(original, altered)
                replacement = artifact("next-runtime")
                current_source = {"commit": "next-source", "input_sha256": "c" * 64, "tracked_dirty": False}
                producer = {"source": dict(current_source), "selection": {
                    "runtime": replacement, "source_sha256": current_source["input_sha256"]}}
                producer_path = root / "runtime-producer.json"
                producer_path.write_text(json.dumps(producer))
                updated = copy.deepcopy(manifest)
                updated["candidate_head"] = current_source["commit"]
                updated["candidate"]["module"] = replacement
                updated["candidate"]["runtime_producer"] = {
                    "path": str(producer_path),
                    "sha256": "sha256:" + hashlib.sha256(producer_path.read_bytes()).hexdigest(),
                }
                # The real R6 validator must reject a file that only claims a
                # matching source digest and runtime, without production evidence.
                with self.assertRaisesRegex(ValueError, "missing current producer binding"):
                    authority.consumed_product_bindings(original, updated)

                verified_producer = dict(producer, scope="package-rgal")
                with patch.object(authority, "validate_producer", return_value=verified_producer) as verify:
                    authority.consumed_product_bindings(original, updated)
                    verify.assert_called_once_with(producer_path.resolve(), Path(authority.__file__).resolve().parents[3])
                    for rejected in (
                        dict(verified_producer, scope="direct"),
                        dict(verified_producer, source=dict(current_source, commit="stale-source")),
                        dict(verified_producer, selection=dict(producer["selection"], runtime=new_module)),
                    ):
                        verify.return_value = rejected
                        with self.assertRaises(ValueError):
                            authority.consumed_product_bindings(original, updated)
                Path(framework["patched_library"]["path"]).write_text("tampered")
                with self.assertRaises(ValueError):
                    authority.consumed_product_bindings(original, manifest)

    def test_consumed_old_key_requires_its_exact_frozen_path_and_hash(self):
        suffix = ".exe" if os.name == "nt" else ""
        module_name = (
            "clipper2next.dll" if os.name == "nt" else "libclipper2next.so.6.1.1"
        )
        clip = {"path": "/original/clip" + suffix, "sha256": "sha256:clip"}
        offset = {"path": "/original/offset" + suffix, "sha256": "sha256:offset"}
        module = {"path": "/original/" + module_name, "sha256": "sha256:module"}
        manifest = {
            "old": {"module": module, "products": {"clip": clip, "offset": offset}},
            "candidate": {
                "core": {"path": "/current/core" + suffix},
                "products": {
                    "clip": dict(clip, path="/current/clip" + suffix),
                    "offset": dict(offset, path="/current/offset" + suffix),
                },
            },
        }
        owner = getattr(authority, "consumed_product_bindings", None)
        self.assertTrue(
            callable(owner), "frozen inventory owner must bind consumed product keys"
        )
        expected = [clip, offset, module]
        owner(expected, manifest)
        for mutation in (
            lambda m: m["old"]["products"].update(
                clip=m["candidate"]["products"]["clip"]
            ),
            lambda m: m["old"]["products"].update(clip=m["old"]["products"]["offset"]),
            lambda m: m["old"]["module"].update(path="/foreign/" + module_name),
            lambda m: m["old"]["products"]["clip"].update(sha256="sha256:foreign"),
            lambda m: m["candidate"]["products"].update(
                clip=m["candidate"]["products"]["offset"]
            ),
        ):
            altered = copy.deepcopy(manifest)
            mutation(altered)
            with self.assertRaises(ValueError):
                owner(expected, altered)
