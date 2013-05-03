"""Find path to OpenCL SDK by various vendors."""
import os
import sys

def main():
  intel_path = os.getenv("INTELOCLSDKROOT", None)
  if intel_path:
    print intel_path
    return 0

  amd_path = os.getenv("AMDAPPSDKROOT", None)
  if amd_path:
    print amd_path
    return 0

  return 1

if __name__ == "__main__":
  sys.exit(main())
