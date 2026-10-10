# PlatformIO generates the complete admin portal from the same sources used by previews.
Import("env")
import pathlib
import sys
project = pathlib.Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "web"))
import build
out = pathlib.Path(env.subst("$PROJECT_BUILD_DIR")) / env.subst("$PIOENV") / "generated"
site = build.build()
build.write_header(site, out / "web_assets.h")
print("embed_web: %d files, %d stored bytes" % (len(site.files), sum(len(d) for _, d, _ in build.encoded_files(site))))
env.Append(CPPPATH=[str(out)])
