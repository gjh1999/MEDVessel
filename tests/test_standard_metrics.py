import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from standard_vessel_metrics import evaluate


class StandardMetricsTests(unittest.TestCase):
    def test_anisotropic_shift(self):
        a = np.zeros((5, 5, 5)); b = a.copy()
        a[1, 2, 2] = 1; b[2, 2, 2] = 1
        m = evaluate(a, b, (3, 2, 1))
        self.assertEqual(m['hd95_mm'], 3)
        self.assertEqual(m['assd_mm'], 3)
        self.assertEqual(m['dice'], 0)

    def test_asymmetric_surface_weighting(self):
        a = np.zeros((9, 3, 3)); b = a.copy()
        a[1, 1, 1] = 1; b[1, 1, 1] = b[5, 1, 1] = 1
        m = evaluate(a, b, (1, 1, 1))
        self.assertEqual(m['assd_mm'], 1.)
        self.assertAlmostEqual(m['hd95_mm'], 3.6)

    def test_identical_and_empty(self):
        a = np.zeros((5, 5, 5)); a[1:4, 2, 2] = 1
        m = evaluate(a, a, (1, 1, 1))
        self.assertEqual((m['dice'], m['cldice'], m['assd_mm']), (1, 1, 0))
        empty = np.zeros_like(a)
        self.assertIsNone(evaluate(a, empty, (1, 1, 1))['hd95_mm'])
        self.assertEqual(evaluate(empty, empty, (1, 1, 1))['dice'], 1)

    def test_invalid(self):
        with self.assertRaises(ValueError):
            evaluate(np.zeros((2, 2, 2)), np.zeros((2, 2, 2)), (0, 1, 1))


if __name__ == '__main__':
    unittest.main()
