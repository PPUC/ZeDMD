from pathlib import Path
import shutil

Import("env")


ASSET_MAP = (
    ("128x16",   ("128x16_border.raw", "128x16_logo.raw")),
    ("192x64",   ("192x64_border.raw", "192x64_logo.raw")),
    ("128x64x2", ("256x64_border.raw", "256x64_logo.raw")),
    ("256x64",   ("256x64_border.raw", "256x64_logo.raw")),
    ("128x64",   ("128x32_border.raw", "128x32_logo.raw")),
    ("128x32",   ("128x32_border.raw", "128x32_logo.raw")),
)


project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
env_name = env.subst("$PIOENV")
staged_data_dir = project_dir / ".pio" / "fsdata" / env_name

if env_name.startswith("ppucdmd_"):
    source_data_dir = project_dir / "data_ppucdmd"
else:
    source_data_dir = Path(env.subst("$PROJECT_DATA_DIR")).resolve()

allowed_files = None
for resolution, asset_files in ASSET_MAP:
    if resolution in env_name:
        allowed_files = asset_files
        break

if allowed_files is None:
    raise ValueError(f"No filesystem asset mapping defined for env '{env_name}'")

if staged_data_dir.exists():
    shutil.rmtree(staged_data_dir)
staged_data_dir.mkdir(parents=True, exist_ok=True)

missing_files = []
for file_name in allowed_files:
    source_file = source_data_dir / file_name
    if source_file.is_file():
        shutil.copy2(source_file, staged_data_dir / file_name)
    else:
        missing_files.append(file_name)

if missing_files:
    missing_list = ", ".join(missing_files)
    raise FileNotFoundError(f"Missing filesystem asset(s): {missing_list}")

env.Replace(PROJECT_DATA_DIR=str(staged_data_dir))
print(f"Filesystem data staged in {staged_data_dir}: {', '.join(allowed_files)}")
