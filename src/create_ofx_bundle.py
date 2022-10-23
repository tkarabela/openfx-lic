import os
import os.path as op
import shutil
import argparse
import sys

parser = argparse.ArgumentParser()
parser.add_argument("linux_build_source_file")
args = parser.parse_args()

linux_build_source_file = args.linux_build_source_file
bundle_dir = op.join(op.dirname(linux_build_source_file), "lic.ofx.bundle")
contents_dir = op.join(bundle_dir, "Contents")

if sys.platform == "linux":
    build_dir = op.join(contents_dir, "Linux-x86-64")
elif sys.platform == "win32":
    build_dir = op.join(contents_dir, "Win64")  # XXX is it always 64bit?
else:
    raise NotImplementedError("unknown platform")

build_file = op.join(build_dir, "lic.ofx")
plist_file = op.join(contents_dir, "Info.plist")

for dir_path in (bundle_dir, contents_dir, build_dir):
    if not op.exists(dir_path):
        os.mkdir(dir_path)

shutil.copy(linux_build_source_file, build_file)

with open(plist_file, "w") as fp:
    fp.write("""\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>lic.ofx</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>0.0.1d1</string>
    <key>CSResourcesFileMapped</key>
    <true/>
</dict>
</plist>
""")
