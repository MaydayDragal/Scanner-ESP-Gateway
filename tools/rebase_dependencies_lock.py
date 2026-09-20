"""Rebind a generated local lock entry to this checkout without changing pins."""
from copy import deepcopy
from pathlib import Path, PurePosixPath, PureWindowsPath

COMPONENT = "espressif/esp_tinyusb"


def _mapping(value, label):
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a mapping")
    return value


def rebase_local_source(lock, source):
    """Pure transformation: only the generated local source may change.

    A registry entry from the pre-override baseline remains authoritative input
    to IDF's ordinary resolver. This helper never converts registry dependencies.
    """
    _mapping(source, "resolved source")
    path = source.get("path")
    if (set(source) != {"type", "path"} or source["type"] != "local"
            or not isinstance(path, str)
            or not (PurePosixPath(path).is_absolute() or PureWindowsPath(path).is_absolute())):
        raise ValueError("resolved source must be a local absolute path from the manifest")
    dependencies = _mapping(_mapping(lock, "lock").get("dependencies"), "dependencies")
    entry = _mapping(dependencies.get(COMPONENT), COMPONENT)
    if entry.get("version") != "2.3.0":
        raise ValueError("expected locked esp_tinyusb version 2.3.0")
    old = _mapping(entry.get("source"), "locked source")
    if old.get("type") == "service":
        if not isinstance(old.get("registry_url"), str):
            raise ValueError("registry source must specify registry_url")
        return deepcopy(lock)
    if (set(old) != {"type", "path"} or old["type"] != "local"
            or not isinstance(old["path"], str) or not old["path"]):
        raise ValueError("expected a generated local source with a path")
    result = deepcopy(lock)
    result["dependencies"][COMPONENT]["source"] = deepcopy(source)
    return result


def prepare_lock(project_root):
    # Imported only by the IDF configure path; pure host tests need no SDK.
    from idf_component_tools.manager import ManifestManager
    from idf_component_tools.lock.manager import LockFile
    from ruamel.yaml import YAML
    import os
    import tempfile

    project_root = Path(project_root).resolve()
    lock_path = project_root / "dependencies.lock"
    if not lock_path.exists():
        return False  # The component manager creates the initial lock normally.
    manifest = ManifestManager(project_root / "main", "main").load()
    requirements = {requirement.name: requirement for requirement in manifest.raw_requirements}
    requirement = requirements.get(COMPONENT)
    if requirement is None or requirement.version_spec != "==2.3.0":
        raise ValueError("main manifest must pin espressif/esp_tinyusb ==2.3.0")
    source = requirement.source
    expected = (project_root / "components/esp_tinyusb").resolve()
    if not source.is_overrider or source.resolved_path != expected:
        raise ValueError("main manifest must override the checked-in components/esp_tinyusb")

    yaml = YAML(typ="safe")
    original = yaml.load(lock_path.read_text(encoding="utf-8"))
    rebased = rebase_local_source(original, source.model_dump())
    # Validate with the installed manager only after removing the stale path:
    # a Windows path cannot be interpreted as a local path by Linux's pathlib.
    LockFile.fromdict(rebased).model_dump()
    if rebased == original:
        return False
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         dir=project_root, prefix=".dependencies-lock-",
                                         suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            yaml.default_flow_style = False
            yaml.width = 2048
            yaml.dump(rebased, output)
        os.replace(temporary, lock_path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return True


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, required=True)
    args = parser.parse_args()
    try:
        changed = prepare_lock(args.project_root)
    except Exception as exc:
        parser.exit(1, f"Cannot rebase local dependency lock: {exc}\n")
    if changed:
        print("Rebased esp_tinyusb local lock source to this checkout; all version pins retained")


if __name__ == "__main__":
    main()
