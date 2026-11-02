import os
import runpy
import sys

TOOL = "C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl/ModWork/tools/worlddiff/test_worlddiff.py"
os.environ["NOLA_INV"] = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "invariants.tsv")

sys.argv = [TOOL] + sys.argv[1:]
runpy.run_path(TOOL, run_name="__main__")
