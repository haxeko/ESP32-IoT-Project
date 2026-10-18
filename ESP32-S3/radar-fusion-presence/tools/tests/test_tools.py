"""Tests of decode.py and report.py on synthetic captures (simulate.py)."""

import os
import tempfile
import unittest

import capture
import decode
import simulate

try:
    import numpy  # noqa: F401
    import report
except ImportError:  # report.py needs numpy
    report = None


def hexs(spaced: str) -> str:
    return spaced.replace(" ", "")


def write_rlog(path: str, bodies, host0: int = 1_700_000_000_000):
    with open(path, "w", encoding="utf-8") as f:
        f.write("# test capture\n")
        for i, body in enumerate(bodies):
            f.write(f"{host0 + i}\t{simulate.record(body)}\n")


class RecordTest(unittest.TestCase):
    def test_checksum(self):
        line = simulate.record("E,1000,mark,seg empty")
        self.assertEqual(decode.parse_record(line), ("E", ["1000", "mark", "seg empty"]))
        with self.assertRaises(decode.ChecksumError):
            decode.parse_record(line[:-1] + ("0" if line[-1] != "0" else "1"))
        self.assertIsNone(decode.parse_record("I (123) boot: ESP-IDF log line"))


class ScheduleTest(unittest.TestCase):
    def test_read_schedule(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "s.txt")
            with open(path, "w", encoding="utf-8") as f:
                f.write("# interference A/B\n300 pwr 2 off   # LD2450 off\n0 mark seg ld2450_on\n\n"
                        "300 mark seg ld2450_off\n")
            self.assertEqual(capture.read_schedule(path),
                             [(0.0, "mark seg ld2450_on"), (300.0, "pwr 2 off"), (300.0, "mark seg ld2450_off")])


class DecodeTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, "t.rlog")

    def tearDown(self):
        self.dir.cleanup()

    def test_frames_replies_and_gaps(self):
        basic = hexs("F4F3F2F1 0D00 02AA02 5100 00 0000 3B 0000 5500 F8F7F6F5")  # doc example
        ld2450 = hexs("AAFF0300 0E03B18610004001") + "00" * 16 + "55CC"
        version = hexs("FDFCFBFA 0C00 A001 0000 0001 0701 16150922 04030201")
        write_rlog(self.path, [
            "B,1000,probe=1.0,idf=v6.0.2,chip=esp32s3,flash=8MB,reset=1",
            "C,1,1000,uart=1,rx=17,tx=18,baud=256000",
            f"D,1,0,100000,{basic}",
            f"D,1,1,200000,{basic}",
            f"D,1,3,400000,{basic}",  # seq 2 lost on the probe
            f"D,2,0,150000,{ld2450}",
            "X,1,300000,FDFCFBFA0200A00004030201",
            f"A,1,4,310000,{version}",
            "S,0,500000,out_drops=0,heap=300000,min_heap=290000",
            "T,600000,1700000000600",
        ])
        with open(self.path, "a", encoding="utf-8") as f:
            f.write("1700000000999\t@E,700000,mark,broken*00\n")  # bad checksum
            f.write("1700000001000\tE (1234) some esp-idf error\n")
        d = decode.decode_rlog(self.path)
        self.assertEqual(len(d.ld2410), 3)
        self.assertEqual(d.ld2410[0]["state"], 2)
        self.assertEqual(d.ld2410[0]["moving_cm"], 81)
        self.assertEqual(len(d.ld2450), 1)
        self.assertEqual((d.ld2450[0]["t1_x_mm"], d.ld2450[0]["t1_y_mm"]), (-782, 1713))
        self.assertEqual(d.summary["sequence_gaps"], {"1": 1})
        self.assertEqual(d.summary["checksum_errors"], 1)
        self.assertEqual(d.summary["other_lines"], 1)
        rx = [r for r in d.replies if r["direction"] == "rx"]
        self.assertEqual(rx[0]["decoded"], "firmware V1.07.22091516 (type 256)")
        self.assertEqual([r["decoded"] for r in d.replies if r["direction"] == "tx"], ["read_version"])
        self.assertEqual(d.sync[0]["pc_ms"], 1700000000600)
        out = os.path.join(self.dir.name, "csv")
        decode.write_outputs(d, out)
        for name in ("ld2410.csv", "ld2450.csv", "replies.csv", "events.csv", "stats.csv", "summary.json"):
            self.assertTrue(os.path.exists(os.path.join(out, name)), name)

    def test_reboot_restarts_sequence(self):
        basic = hexs("F4F3F2F1 0D00 02AA02 5100 00 0000 3B 0000 5500 F8F7F6F5")
        write_rlog(self.path, ["B,1000,probe=1", f"D,1,0,100000,{basic}", f"D,1,1,200000,{basic}",
                               "B,1000,probe=1", f"D,1,0,100000,{basic}"])
        d = decode.decode_rlog(self.path)
        self.assertEqual(d.summary["sequence_gaps"], {})
        self.assertEqual([r["boot"] for r in d.ld2410], [1, 1, 2])


@unittest.skipIf(report is None, "numpy missing")
class ReportTest(unittest.TestCase):
    def test_synthetic_scenarios(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "s.rlog")
            simulate.simulate(path, [("empty", 90), ("fan", 90), ("person_still", 90)], seed=3)
            d = decode.decode_rlog(path)
            self.assertAlmostEqual(len(d.ld2410), 2700, delta=5)
            boot_end = {1: max(r["t_s"] for r in d.ld2410)}
            segs = report.find_segments(d.events, boot_end)
            self.assertEqual(sorted(segs), ["empty", "fan", "person_still"])

            empty = report.ld2410_arrays(report.in_segment(d.ld2410, segs["empty"]))
            fan = report.ld2410_arrays(report.in_segment(d.ld2410, segs["fan"]))
            # The fan raises the moving energy of gate 4, not of gate 8
            d4, p4 = report.ks_test(empty[1][:, 4], fan[1][:, 4])
            d8, _ = report.ks_test(empty[1][:, 8], fan[1][:, 8])
            self.assertGreater(d4, 0.8)
            self.assertLess(p4, 1e-6)
            self.assertLess(d8, 0.15)
            # Its 12 s swing is found by the autocorrelation
            period = report.dominant_period(report.uniform(fan[0], fan[1][:, 4]))
            self.assertIsNotNone(period)
            self.assertAlmostEqual(period[0], 12.0, delta=0.6)
            # The simulated breathing (0.25 Hz) puts most still power of gate 3 in the 0.1-0.5 Hz band
            still = report.ld2410_arrays(report.in_segment(d.ld2410, segs["person_still"]))
            self.assertGreater(report.band_share(report.uniform(still[0], still[2][:, 3])), 0.6)

            text = report.report(path, ["empty", "fan"])
            self.assertIn("## Segment comparison", text)
            self.assertIn("LD2410 (UART1, boot 1)", text)

    def test_auc_and_windows(self):
        self.assertEqual(report.auc(numpy.array([1.0, 2, 3]), numpy.array([4.0, 5, 6])), 1.0)
        self.assertEqual(report.auc(numpy.array([4.0, 5, 6]), numpy.array([1.0, 2, 3])), 0.0)
        self.assertEqual(report.auc(numpy.array([1.0, 1]), numpy.array([1.0, 1])), 0.5)
        t = numpy.arange(0, 60, 0.1)
        self.assertEqual(len(report.window_medians(t, numpy.ones_like(t))), 6)

    def test_lag_between_radars(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "w.rlog")
            simulate.simulate(path, [("walk", 120)], seed=5)
            d = decode.decode_rlog(path)
            text = report.lag_section(d.ld2410, d.ld2450)[0]
            self.assertIn("lags the LD2450 range", text)
            # simulate.py sends the LD2450 report 37 ms after the LD2410 one for the same position
            lag_ms = float(text.split(" by ")[1].split(" ms")[0])
            self.assertLess(abs(lag_ms + 37), 60)

    def test_capture_with_reboot(self):
        # Two boots in one file: times restart, the analysis must not mix them
        with tempfile.TemporaryDirectory() as tmp:
            first, second, both = (os.path.join(tmp, n) for n in ("a.rlog", "b.rlog", "ab.rlog"))
            simulate.simulate(first, [("fan", 130)], seed=1)
            simulate.simulate(second, [("fan", 60)], seed=2)
            with open(both, "w", encoding="utf-8") as out:
                for name in (first, second):
                    with open(name, encoding="utf-8") as f:
                        out.write(f.read())
            d = decode.decode_rlog(both)
            self.assertEqual(len(d.summary["boots"]), 2)
            t, m, _, _ = report.ld2410_arrays(d.ld2410)
            self.assertEqual(len(report.blocks(t)), 2)
            period = report.dominant_period(report.uniform(*report.longest_block(t, m[:, 4])))
            self.assertAlmostEqual(period[0], 12.0, delta=0.6)
            self.assertIn("## LD2410 energy: fan", report.report(both))

    def test_effective_n(self):
        rng = numpy.random.default_rng(1)
        white = rng.normal(size=5000)
        smooth = numpy.convolve(white, numpy.ones(20) / 20, mode="valid")
        self.assertGreater(report.effective_n(white), 4000)
        self.assertLess(report.effective_n(smooth), 500)


if __name__ == "__main__":
    unittest.main()
