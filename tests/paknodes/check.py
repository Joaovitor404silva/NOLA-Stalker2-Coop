import os, runpy, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TOOL = "C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl/ModWork/s2coop/tools/check_pak_nodes.py"

sys.argv = [TOOL, "--data", os.path.join(REPO, "data")] + sys.argv[1:]
runpy.run_path(TOOL, run_name="__main__")
