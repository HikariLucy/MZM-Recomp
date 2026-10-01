import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "check_m4_matrix", ROOT / "scripts" / "check-m4-matrix.py")
cm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cm)

HEAD = ("| Area | Feature / Route | Status | Evidence | Automation | Remaining Risk |\n"
        "|---|---|---|---|---|---|\n")


def doc(rows, stated="**{t} total — {p} PASS, {a} PARTIAL, {b} BLOCKED, {u} UNVERIFIED**",
        counts=None):
    t, p, a, b, u = counts or (len(rows), 0, 0, 0, 0)
    return ("Current row counts: " + stated.format(t=t, p=p, a=a, b=b, u=u)
            + "\n\n" + HEAD + "\n".join(rows) + "\n")


def row(area="A", feat="f", status="PASS", extra=""):
    return f"| {area} | {feat} | {status} | ev | auto | risk |{extra}"


class MatrixTests(unittest.TestCase):
    def errors(self, text):
        return cm.check(text)[2]

    def test_real_matrix_is_valid(self):
        text = (ROOT / "docs" / "M4-COMPATIBILITY-MATRIX.md").read_text(encoding="utf-8")
        self.assertEqual(self.errors(text), [])

    def test_counts_and_qualifier(self):
        rows = [row(feat="1"), row(feat="2", status="PASS (route-scoped)"),
                row(feat="3", status="PARTIAL")]
        total, counts, errs, _ = cm.check(doc(rows, counts=(3, 2, 1, 0, 0)))
        self.assertEqual((total, counts["PASS"], counts["PARTIAL"]), (3, 2, 1))
        self.assertEqual(errs, [])

    def test_unknown_status_fails(self):
        self.assertTrue(any("unknown status" in e for e in
                            self.errors(doc([row(status="PASSED")], counts=(1, 0, 0, 0, 0)))))

    def test_missing_trailing_pipe_fails(self):
        bad = "| A | f | PASS | ev | auto | risk"
        self.assertTrue(any("does not end" in e for e in
                            self.errors(doc([bad], counts=(1, 1, 0, 0, 0)))))

    def test_wrong_column_count_fails(self):
        self.assertTrue(any("columns" in e for e in
                            self.errors(doc(["| A | f | PASS | ev |"], counts=(1, 1, 0, 0, 0)))))

    def test_duplicate_row_fails(self):
        rows = [row(), row()]
        self.assertTrue(any("duplicate" in e for e in
                            self.errors(doc(rows, counts=(2, 2, 0, 0, 0)))))

    def test_stated_counts_mismatch_fails(self):
        self.assertTrue(any("stated counts" in e for e in
                            self.errors(doc([row()], counts=(1, 0, 1, 0, 0)))))

    def test_escaped_and_code_pipes_do_not_split(self):
        r = "| A | f `a|b` | PASS | x \\| y | auto | risk |"
        total, _, errs, _ = cm.check(doc([r], counts=(1, 1, 0, 0, 0)))
        self.assertEqual((total, errs), (1, []))


if __name__ == "__main__":
    unittest.main()
