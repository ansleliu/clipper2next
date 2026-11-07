import copy
import json
import tempfile
import unittest
from pathlib import Path
import struct
from unittest import mock

from benchmarks.tools.common.evidence_identity import sha256_file
from benchmarks.tools.common.matched_input_authority import (
    producing_observation,
    kernel_rva,
    kernel_records,
)


class MatchedInputAuthorityTests(unittest.TestCase):
    def test_cocircular_entry_requires_candidate_integer_implementation(self):
        name = "try_project_cocircular_quad"
        prefix = f"_ZN12clipper2next8internal{len(name)}{name}E".encode()
        integer = prefix + b"RN8geotypes6Point2IlEE"
        floating = prefix + b"RN8geotypes6Point2IlEERNS1_IdEEEd"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "actual.so"
            path.write_bytes(self.elf_module([(integer, 2, 1, 0x100), (floating, 2, 1, 0x200)]))
            record = {"format": "elf-symbol", "module": {"path": str(path), "sha256": sha256_file(path)}}
            self.assertEqual(kernel_rva(record, side="candidate", cocircular_quad=True), 0x100)
            with self.assertRaises(ValueError):
                kernel_rva(record, side="old", cocircular_quad=True)
            with self.assertRaises(ValueError):
                kernel_rva(record, side="candidate", sweep=True, cocircular_quad=True)
            path.write_bytes(self.elf_module([(floating, 2, 1, 0x200)]))
            record["module"]["sha256"] = sha256_file(path)
            with self.assertRaises(ValueError):
                kernel_rva(record, side="candidate", cocircular_quad=True)

    def test_sweep_entry_requires_candidate_integer_implementation(self):
        prefix = b"_ZN12clipper2next8internal32execute_sweep_triangulation_intoE"
        integer = prefix + b"RN8geotypes6Point2IlEE"
        floating = prefix + b"RN8geotypes6Point2IlEERNS1_IdEEEd"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "actual.so"
            for symbols, accepted in (
                ([(integer, 2, 1, 0x100), (floating, 2, 1, 0x200)], True),
                ([(floating, 2, 1, 0x200)], False),
                ([(integer+b".cold", 2, 1, 0x100)], False),
                ([(integer, 2, 1, 0x100), (integer+b"i", 2, 1, 0x200)], False),
            ):
                path.write_bytes(self.elf_module(symbols))
                record = {"format": "elf-symbol", "module": {"path": str(path), "sha256": sha256_file(path)}}
                if accepted:
                    self.assertEqual(kernel_rva(record, side="candidate", sweep=True), 0x100)
                else:
                    with self.assertRaises(ValueError):
                        kernel_rva(record, side="candidate", sweep=True)
            with self.assertRaises(ValueError):
                kernel_rva(record, side="old", sweep=True)

    def test_coff_sweep_entry_excludes_the_scaled_output_overload(self):
        integer = b"?execute_sweep_triangulation_into@internal@clipper2next@@YAXPoint2@_J@geotypes@@"
        floating = integer + b"Point2@N@geotypes@@N@Z"
        body = bytes(range(31, 47))
        module = bytearray(256)+body
        struct.pack_into("<I", module, 0x3c, 64)
        struct.pack_into("<H", module, 70, 1)
        struct.pack_into("<IIII", module, 96, len(body), 0x1000, len(body), 256)
        with tempfile.TemporaryDirectory() as directory:
            library, obj = Path(directory)/"actual.dll", Path(directory)/"actual.obj"
            library.write_bytes(module)
            for symbols, accepted in (([integer, floating], True), ([floating], False)):
                obj.write_bytes(self.coff_object(symbols, body))
                record = {"format": "coff-body", "module": {"path": str(library), "sha256": sha256_file(library)},
                          "object": {"path": str(obj), "sha256": sha256_file(obj)}}
                if accepted:
                    self.assertEqual(kernel_rva(record, side="candidate", sweep=True), 0x1000)
                else:
                    with self.assertRaises(ValueError):
                        kernel_rva(record, side="candidate", sweep=True)

    def test_kernel_attribution_cannot_decode_a_different_runtime_from_its_side(self):
        cold = [f"case-{i}/sweep/cold" for i in range(20)]
        rows = [{"side": side, "row": row} for side in ("old", "candidate") for row in cold]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"rows.json"
            path.write_text(json.dumps(rows), encoding="utf-8")
            runtime, foreign = Path(directory)/"actual.so", Path(directory)/"different.so"
            data = self.elf_module([
                (b"_ZN12clipper2next8internal23run_triangulation_sweepERNS0_21triangulation_contextE", 2, 1, 0x100),
                (b"_ZN12clipper2next8internal27execute_triangulation_graphERNS0_29triangulation_operation_leaseE", 2, 1, 0x200),
            ])
            runtime.write_bytes(data)
            foreign.write_bytes(data)
            module = {"path": str(runtime), "sha256": sha256_file(runtime)}
            manifest = {"kernel_entry": {"path": str(path), "sha256": sha256_file(path)}}
            for side in ("old", "candidate"):
                manifest[side] = {"module": module, "kernel_attribution": {"format": "elf-symbol", "module": dict(module)}}
            manifest["candidate"]["kernel_attribution"]["module"]["path"] = str(foreign)
            with mock.patch("benchmarks.tools.common.matched_input_authority.supplement_rows", return_value=cold):
                with self.assertRaisesRegex(ValueError, "runtime differs"):
                    kernel_records(manifest, [])

    def test_current_graph_entry_is_selected_independently_from_legacy_sweep(self):
        legacy = b"_ZN12clipper2next8internal23run_triangulation_sweepERNS0_21triangulation_contextE"
        graph = b"_ZN12clipper2next8internal27execute_triangulation_graphERNS0_29triangulation_operation_leaseERKSt6vectorIS3_INS_5PointIlEESaIS5_EESaIS7_EEbRNS_17TriangulateResultE"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "actual.so"
            path.write_bytes(self.elf_module([(legacy, 2, 1, 0x100), (graph, 2, 1, 0x200)]))
            record = {"format": "elf-symbol", "module": {"path": str(path), "sha256": sha256_file(path)}}
            self.assertEqual(kernel_rva(record, side="old"), 0x100)
            self.assertEqual(kernel_rva(record, side="candidate"), 0x200)

    def test_candidate_entry_rejects_sweep_clones_foreign_and_ambiguous_graphs(self):
        graph = b"_ZN12clipper2next8internal27execute_triangulation_graphERNS0_29triangulation_operation_leaseE"
        legacy = b"_ZN12clipper2next8internal23run_triangulation_sweepERNS0_21triangulation_contextE"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "actual.so"
            for symbols in (
                [(legacy, 2, 1, 0x100)],
                [(graph+b".cold", 2, 1, 0x200)],
                [(b"_ZN7foreign8internal27execute_triangulation_graphE", 2, 1, 0x200)],
                [(graph, 2, 0, 0x200)],
                [(graph, 1, 1, 0x200)],
                [(graph, 2, 1, 0x200), (graph+b"i", 2, 1, 0x300)],
            ):
                with self.subTest(symbols=symbols):
                    path.write_bytes(self.elf_module(symbols))
                    record = {"format": "elf-symbol", "module": {"path": str(path), "sha256": sha256_file(path)}}
                    with self.assertRaises(ValueError):
                        kernel_rva(record, side="candidate")
            with self.assertRaises(ValueError):
                kernel_rva(record, side="automatic")

    @staticmethod
    def coff_object(symbols, body):
        names = bytearray(b"\0\0\0\0")
        entries = bytearray()
        for symbol in symbols:
            index = len(names)
            names.extend(symbol+b"\0")
            entries.extend(struct.pack("<8sIhHBB", b"\0"*4+struct.pack("<I", index), 0, 1, 0x20, 2, 0))
        struct.pack_into("<I", names, 0, len(names))
        data = bytearray(60)+body+entries+names
        struct.pack_into("<HHIIIHH", data, 0, 0x8664, 1, 0, 60+len(body), len(symbols), 0, 0)
        struct.pack_into("<III", data, 36, len(body), 60, 0)
        return data

    def test_current_coff_graph_body_requires_its_exact_namespace_and_unique_function(self):
        graph = b"?execute_triangulation_graph@internal@clipper2next@@YAXXZ"
        legacy = b"?run_triangulation_sweep@internal@clipper2next@@YAXXZ"
        body = bytes(range(31, 47))
        module = bytearray(256)+body
        struct.pack_into("<I", module, 0x3c, 64)
        struct.pack_into("<H", module, 70, 1)
        struct.pack_into("<IIII", module, 96, len(body), 0x1000, len(body), 256)
        with tempfile.TemporaryDirectory() as directory:
            library, obj = Path(directory)/"actual.dll", Path(directory)/"actual.obj"
            library.write_bytes(module)
            for symbols, accepted in (([graph], True), ([legacy], False),
                                      ([b"?execute_triangulation_graph@foreign@@YAXXZ"], False),
                                      ([graph, graph+b"i"], False)):
                with self.subTest(symbols=symbols):
                    obj.write_bytes(self.coff_object(symbols, body))
                    record = {"format": "coff-body", "module": {"path": str(library), "sha256": sha256_file(library)},
                              "object": {"path": str(obj), "sha256": sha256_file(obj)}}
                    if accepted:
                        self.assertEqual(kernel_rva(record, side="candidate"), 0x1000)
                    else:
                        with self.assertRaises(ValueError):
                            kernel_rva(record, side="candidate")

    @staticmethod
    def elf_module(symbols):
        # Real ELF64 symbol/string layouts; symbol names are independent ABI data.
        names = bytearray(b"\0")
        entries = bytearray()
        for name, info, section, value in symbols:
            offset = len(names)
            names.extend(name + b"\0")
            entries.extend(struct.pack("<IBBHQQ", offset, info, 0, section, value, 16))
        data = bytearray(320) + entries + names
        data[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<Q", data, 40, 64)
        struct.pack_into("<HH", data, 58, 64, 4)
        struct.pack_into(
            "<IIQQQQIIQQ", data, 64 + 2 * 64, 0, 2, 0, 0, 320, len(entries), 3, 0, 8, 24
        )
        struct.pack_into(
            "<IIQQQQIIQQ",
            data,
            64 + 3 * 64,
            0, 3, 0, 0, 320 + len(entries), len(names), 0, 0, 1, 0,
        )
        return data

    def test_proof_role_command_and_uuid_have_independent_expectations(self):
        exe = {"path": "/public.exe", "sha256": "sha256:exe"}
        module = {"path": "/clipper2next.dll", "sha256": "sha256:module"}
        command = [exe["path"], "--fixtures", "/fixed.txt", "--mode", "records"]
        step = {
            "role": "public-records",
            "side": "candidate",
            "run_id": "fresh-id",
            "command": command,
            "observation": {
                "pid": 123,
                "facility": "OS query",
                "command": command,
                "run_id": "fresh-id",
                "executable": exe,
                "modules": [module],
            },
        }
        producing_observation(step, "public-records", "candidate", command, exe, module)
        for mutate in (
            lambda p: p.update(role="worker-placement"),
            lambda p: p.update(run_id=None),
            lambda p: p["observation"].pop("run_id"),
            lambda p: p.update(command=[exe["path"], "--mode", "placement"]),
            lambda p: p.update(side="old"),
        ):
            value = copy.deepcopy(step)
            mutate(value)
            with self.assertRaises(ValueError):
                producing_observation(
                    value, "public-records", "candidate", command, exe, module
                )

    def test_elf_attribution_uses_actual_defined_function_symbol_not_a_claimed_rva(
        self,
    ):
        # ELF64 little-endian ABI fixture: null, text, symbol table, linked strings.
        primary = (
            b"_ZN12clipper2next8internal23run_triangulation_sweep"
            b"ERNS0_21triangulation_contextE"
        )
        data = self.elf_module([(primary, 2, 1, 0x81D50)])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "actual.so"
            path.write_bytes(data)
            record = {
                "format": "elf-symbol",
                "module": {"path": str(path), "sha256": sha256_file(path)},
                "rva": "0xdead",
            }
            self.assertEqual(kernel_rva(record, side="old"), 0x81D50)
            struct.pack_into(
                "<H", data, 326, 0
            )  # Undefined symbols cannot prove execution entry.
            path.write_bytes(data)
            record["module"]["sha256"] = sha256_file(path)
            with self.assertRaisesRegex(ValueError, "not unique"):
                kernel_rva(record, side="old")

    def test_elf_ignores_nested_template_and_foreign_namespace_fragments(self):
        primary = (
            b"_ZN12clipper2next8internal23run_triangulation_sweep"
            b"ERNS0_21triangulation_contextE"
        )
        nested = (
            b"_ZSt16__introsort_loopIPiZNS12clipper2next8internal23run_triangulation_sweep"
            b"ERNS0_21triangulation_contextEEUlE_EvT_"
        )
        foreign = (
            b"_ZN7foreign8internal23run_triangulation_sweepERNS0_21triangulation_contextE"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "actual.so"
            for symbols, expected in (
                (
                    [
                        (primary, 2, 1, 0xA7040), (nested, 2, 1, 0xA6B70),
                        (foreign, 2, 1, 0x1234), (primary, 2, 0, 0x8888),
                    ],
                    0xA7040,
                ),
                ([(nested, 2, 1, 0xA6B70), (foreign, 2, 1, 0x1234)], None),
                ([(primary, 1, 1, 0xA7040)], None),
                ([(primary, 2, 0, 0xA7040)], None),
                ([(primary, 2, 1, 0xA7040), (primary, 2, 1, 0xA7050)], None),
            ):
                with self.subTest(symbols=symbols):
                    path.write_bytes(self.elf_module(symbols))
                    record = {
                        "format": "elf-symbol",
                        "module": {"path": str(path), "sha256": sha256_file(path)},
                        "rva": "0xdead",
                    }
                    if expected is None:
                        with self.assertRaisesRegex(ValueError, "not unique"):
                            kernel_rva(record, side="old")
                    else:
                        self.assertEqual(expected, kernel_rva(record, side="old"))
