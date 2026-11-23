#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
for source in LavenderQUPI2C.cpp LavenderQUPI2C.h LavenderRPMGLINK.cpp LavenderRPMGLINK.h; do
  test -s "$SCRIPT_DIR/support/$source"
  cp "$SCRIPT_DIR/support/$source" "iokit/Kernel/arm/$source"
done
python3 - <<'PY'
from pathlib import Path
expert=Path("iokit/Kernel/arm/SweetPlatformExpert.cpp")
files=Path("iokit/conf/files.arm64")
if not expert.is_file() or not files.is_file():
    raise SystemExit("SweetPlatformExpert/files.arm64 missing")
s=expert.read_text()
anchor="#include <IOKit/IOLib.h>"
add="""#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSCollectionIterator.h>
#if defined(ARM64_BOARD_CONFIG_LAVENDER)
extern "C" void lavender_register_rpm_glink_driver(void);
extern "C" void lavender_register_qup_i2c_driver(void);
#endif"""
if "lavender_register_rpm_glink_driver" not in s:
    if anchor not in s: raise SystemExit("SweetPlatformExpert include anchor changed")
    s=s.replace(anchor,add,1)
anchor="\tif (!IODTPlatformExpert::start(provider)) {\n\t\treturn false;\n\t}\n"
add=anchor+"""#if defined(ARM64_BOARD_CONFIG_LAVENDER)
\t/* Register power transport before QUP; QUP's clock vote depends on it. */
\tlavender_register_rpm_glink_driver();
\tlavender_register_qup_i2c_driver();
#endif
"""
if "lavender_register_rpm_glink_driver();" not in s:
    if anchor not in s: raise SystemExit("SweetPlatformExpert start anchor changed")
    s=s.replace(anchor,add,1)
anchor="\tcreateNubs(this, IODTFindMatchingEntries(root, kIODTExclusive, excludeList()));\n"
add=anchor+"""#if defined(ARM64_BOARD_CONFIG_LAVENDER)
\t/* Publish SDM660 arm-io children, excluding the GIC owned by PE. */
\tIORegistryEntry *armio = root->childFromPath("arm-io", gIODTPlane);
\tif (armio != NULL) {
\t\tcreateNubs(this, IODTFindMatchingEntries(armio, kIODTExclusive, "('gic')"));
\t\tarmio->release();
\t}
\t/* RPM GLINK is a root /soc child, not an /arm-io device. */
\tIORegistryEntry *soc = root->childFromPath("soc", gIODTPlane);
\tif (soc != NULL) {
\t\tIORegistryEntry *rpmGlink = soc->childFromPath("rpm-glink", gIODTPlane);
\t\tif (rpmGlink != NULL) {
\t\t\tOSArray *rpmNubs = OSArray::withObjects((const OSObject **)&rpmGlink, 1, 1);
\t\t\tif (rpmNubs != NULL) {
\t\t\t\tOSIterator *rpmIterator = OSCollectionIterator::withCollection(rpmNubs);
\t\t\t\trpmNubs->release();
\t\t\t\tif (rpmIterator != NULL) createNubs(this, rpmIterator);
\t\t\t}
\t\t\trpmGlink->release();
\t\t}
\t\tsoc->release();
\t}
#endif
"""
if "rpmGlink = soc->childFromPath" not in s:
    if anchor not in s: raise SystemExit("SweetPlatformExpert nub anchor changed")
    s=s.replace(anchor,add,1)
expert.write_text(s)
f=files.read_text()
for line in ("iokit/Kernel/arm/LavenderRPMGLINK.cpp optional iokitcpp",
             "iokit/Kernel/arm/LavenderQUPI2C.cpp optional iokitcpp"):
    if line not in f:
        anchor="iokit/Kernel/arm/SweetPlatformExpert.cpp optional iokitcpp"
        if anchor not in f: raise SystemExit("files.arm64 anchor changed")
        f=f.replace(anchor,anchor+"\n"+line,1)
files.write_text(f)
print("staged Lavender native IOKit drivers")
PY
