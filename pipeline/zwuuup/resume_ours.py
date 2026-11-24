"""Resume any of the pipeline's scripts left paused by a GPU borrow."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gpu_share import resume_leftovers, our_scripts, suspended
print("resumed:", resume_leftovers())
print("still suspended:", [p for p in our_scripts() if suspended(p)])
