#!/usr/bin/env python3
"""Deprecated: use gen-cjk32.py (native 32×32)."""
import runpy, sys
from pathlib import Path
sys.argv[0] = str(Path(__file__).with_name("gen-cjk32.py"))
runpy.run_path(str(Path(__file__).with_name("gen-cjk32.py")), run_name="__main__")
