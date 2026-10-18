"""Tests of tools/hlk_proto.py against the vectors shared with the C++ host tests."""

import os
import re
import unittest

import hlk_proto as hp

VECTORS = os.path.join(os.path.dirname(__file__), "..", "..", "test", "vectors", "hlk_vectors.txt")


def no_spaces(s: str) -> str:
    return re.sub(r"\s+", "", s)


def load_vectors():
    with open(VECTORS, encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = [x.strip() for x in line.split("|")]
            events = [e.strip() for e in fields[2].split(";") if e.strip()]
            counters = fields[3] if len(fields) > 3 else ""
            yield fields[0], bytes.fromhex(no_spaces(fields[1])), events, counters


class VectorTest(unittest.TestCase):
    def test_vectors(self):
        count = 0
        for name, stream, expected, counters in load_vectors():
            count += 1
            for chunk in (max(len(stream), 1), 1, 7):
                with self.subTest(name=name, chunk=chunk):
                    p = hp.Parser()
                    events = []
                    for pos in range(0, len(stream), chunk):
                        events += [hp.event_text(f) for f in p.feed(stream[pos:pos + chunk])]
                    self.assertEqual(no_spaces(" ; ".join(events)), no_spaces(" ; ".join(expected)))
                    if counters:
                        self.assertEqual(no_spaces(p.counters()), no_spaces(counters))
        self.assertGreater(count, 30)


def ack(hex_text: str) -> hp.Ack:
    frames = hp.Parser().feed(bytes.fromhex(no_spaces(hex_text)))
    assert len(frames) == 1 and frames[0].kind == hp.KIND_ACK
    return hp.decode_ack(frames[0].payload)


class ReplyTest(unittest.TestCase):
    def test_versions(self):
        self.assertEqual(hp.decode_version(ack("FDFCFBFA 0C00 A001 0000 0001 0701 16150922 04030201")),
                         (0x0100, "V1.07.22091516"))
        self.assertEqual(hp.decode_version(ack("FDFCFBFA 0C00 A001 0000 0000 0201 16240622 04030201")),
                         (0, "V1.02.22062416"))
        self.assertEqual(hp.format_version(0x0244, 0x24073110), "V2.44.24073110")

    def test_params(self):
        p = hp.decode_ld2410_params(
            ack("FDFCFBFA 1C00 6101 0000 AA080808 141414141414141414 191919191919191919 0500 04030201"))
        self.assertEqual(p["gates"], 9)
        self.assertEqual(p["moving_threshold"], [20] * 9)
        self.assertEqual(p["still_threshold"], [25] * 9)
        self.assertEqual(p["no_one_duration_s"], 5)

    def test_zones_two_complement(self):
        z = hp.decode_ld2450_zones(
            ack("FDFCFBFA 1E00 C101 0000 0100 E803E80318FC8813 0000000000000000 0000000000000000 04030201"))
        self.assertEqual(z["type"], 1)
        self.assertEqual(z["zones"][0], [1000, 1000, -1000, 5000])

    def test_mac(self):
        self.assertEqual(hp.decode_mac(ack("FDFCFBFA 0A00 A501 0000 8F272EB80F65 04030201")),
                         ("8F:27:2E:B8:0F:65", True))
        self.assertFalse(hp.decode_mac(ack("FDFCFBFA 0A00 A501 0000 080504030201 04030201"))[1])

    def test_describe(self):
        self.assertEqual(hp.describe_ack(ack("FDFCFBFA 0600 9101 0000 0200 04030201")), "multi target")
        self.assertEqual(hp.describe_ack(ack("FDFCFBFA 0400 6401 0100 04030201")), "failed")
        self.assertEqual(hp.describe_ack(ack("FDFCFBFA 0600 AB01 0000 0100 04030201")), "gate 0.2 m")

    def test_build_command(self):
        self.assertEqual(hp.build_command(hp.CMD_ENABLE_CONFIG, b"\x01\x00").hex().upper(),
                         "FDFCFBFA0400FF00010004030201")
        self.assertEqual(hp.build_command(hp.LD2450_CMD_READ_ZONES).hex().upper(), "FDFCFBFA0200C10004030201")

    def test_signed(self):
        self.assertEqual(hp.ld2450_signed(bytes([0x00, 0x80])), 0)
        self.assertEqual(hp.ld2450_signed(bytes([0xFF, 0x7F])), -32767)
        self.assertEqual(hp.s16(bytes([0x18, 0xFC])), -1000)

    def test_input_offset(self):
        stream = bytes.fromhex(no_spaces("00 FDFCFBFA 0400 FE01 0000 04030201 0000 FDFCFBFA 0400 A301 0000 04030201 00"))
        p = hp.Parser()
        self.assertEqual([f.input_offset for f in p.feed(stream)], [15, 31])
        self.assertEqual(p.garbage, 4)


if __name__ == "__main__":
    unittest.main()
