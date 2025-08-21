import os, shutil, json, zipfile, re, tomllib, pprint
from pathlib import Path

def slugify(text: str) -> str:
    text = text.strip()
    text = re.sub(r'[\s_]+', '_', text)
    text = re.sub(r'[^a-zA-Z0-9_]', '', text)
    return text

def get_description() -> str:
    description = re.sub(r'[\n]+', ' ', mod_toml["manifest"]["description"]).strip()
    if len(description) > 0 and len(description) <= 256:
        return description
    return mod_toml["manifest"]["short_description"]

def get_website_url() -> str:
    if "website_url" in mod_toml["manifest"]:
        return mod_toml["manifest"]["website_url"]
    return None

def get_package_manifest() ->dict[str, str]:
    return {
        "name": slugify(mod_toml["manifest"]["display_name"]),
        "version_number": mod_toml["manifest"]["version"],
        "website_url": get_website_url(),
        "description": get_description(),
        "dependencies": []
    }

def create_manifest(path: Path) -> dict:
    manifest = get_package_manifest()
    path.write_text(json.dumps(manifest, indent=4))
    return True

def update_manifest(path: Path) -> dict:
    manifest = get_package_manifest();
    del manifest["name"]
    del manifest["dependencies"]

    current_manifest: dict[str, str] = json.loads(path.read_text());
    current_manifest.update(manifest)
    path.write_text(json.dumps(current_manifest, indent=4))
    return True

def create_readme(path: Path):
    readme_str = f"# {mod_toml['manifest']['display_name']}\n\n{mod_toml['manifest']['description']}"
    path.write_text(readme_str)
    return True

def create_changelog(path: Path):
    readme_str = f"# CHANGELOG\n\n## Version 1.0.0\n\n* Initial release\n"
    path.write_text(readme_str)
    return True

def copy_file(src_path: Path, dst_path: Path) -> bool:
    if (src_path.is_file()):
        shutil.copy(src_path, dst_path)
        return True
    return False

def create_archive(package_dir: Path, dst_path: Path):
    new_zip = zipfile.ZipFile(dst_path, 'w')
    for i in os.listdir(package_dir):
        if i == "images":
            continue
        src_path = package_dir.joinpath(i)
        new_zip.write(src_path, i)
    new_zip.close()
    return True

def create_logger():
    error = False
    def func(message, result):
        nonlocal error
        if result:
            print("\033[32m[OK]\033[0m " + message)
        else:
            error = True
            print("\033[31m[ERROR]\033[0m " + message)
    def did_error():
        return error
    return func, did_error

def create_package():
    print("Creating Thunderstore package...")

    log, did_error = create_logger()

    root_dir = Path(__file__).parent.parent
    build_dir = root_dir.joinpath("build")
    package_dir = root_dir.joinpath("dist")

    global mod_toml
    mod_toml = tomllib.loads(root_dir.joinpath("mod.toml").read_text())

    log(f"Create package dir at {package_dir.relative_to(root_dir)}",
        os.makedirs(package_dir, exist_ok=True) or True)

    manifest_file = package_dir.joinpath("manifest.json")
    if manifest_file.is_file():
        log(f"Create manifest at {manifest_file.relative_to(root_dir)}",
            create_manifest(manifest_file))
    else:
        log(f"Update manifest at {manifest_file.relative_to(root_dir)}",
            update_manifest(manifest_file))

    readme_file = package_dir.joinpath("README.md")
    if not readme_file.is_file():
        log(f"Create readme at {readme_file.relative_to(root_dir)}",
            create_readme(readme_file))

    changelog_file = package_dir.joinpath("CHANGELOG.md")
    if not changelog_file.is_file():
        log(f"Create changelog at {changelog_file.relative_to(root_dir)}",
            create_changelog(changelog_file))

    icon_file_src = root_dir.joinpath("thumb.png")
    icon_file_dst = package_dir.joinpath("icon.png")
    log(f"Copy icon from {icon_file_src.relative_to(root_dir)} to {icon_file_dst.relative_to(root_dir)}",
        copy_file(icon_file_src, icon_file_dst))

    nrm_file_src = build_dir.joinpath(mod_toml["inputs"]["mod_filename"] + ".nrm")
    nrm_file_dst = package_dir.joinpath(mod_toml["inputs"]["mod_filename"] + ".nrm")
    log(f"Copy nrm from {nrm_file_src.relative_to(root_dir)} to {nrm_file_dst.relative_to(root_dir)}",
        copy_file(nrm_file_src, nrm_file_dst))

    for native_library in mod_toml["manifest"]["native_libraries"]:
        for extension in [".so", ".dll", ".dylib"]:
            extlib_dir = "bin" if extension == ".dll" else "lib"
            extlib_src = build_dir.joinpath(extlib_dir, native_library["name"] + extension)
            extlib_dst = package_dir.joinpath(native_library["name"] + extension)
            log(f"Copy extlib from {extlib_src.relative_to(root_dir)} to {extlib_dst.relative_to(root_dir)}",
                copy_file(extlib_src, extlib_dst))

    package_file = root_dir.joinpath(mod_toml["manifest"]["id"] + ".thunderstore.zip")
    if did_error():
        log("Some files were missing, skipping creating mod package", False)
    else:
        log("Creating mod package",
            create_archive(package_dir, package_file))


if __name__ == '__main__':
    create_package()
