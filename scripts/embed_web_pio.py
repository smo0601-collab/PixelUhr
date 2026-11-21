"""PlatformIO pre-build hook: regenerates src/web_assets.h from data/."""
import sys
from pathlib import Path

Import("env")
project = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "tools"))
import embed_web  # noqa: E402

embed_web.build(project)
