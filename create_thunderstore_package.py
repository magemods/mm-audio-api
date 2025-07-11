import subprocess, os, shutil, json, zipfile, re
from pathlib import Path

import make_python_functions as bm

info = bm.ModInfo("./mod.toml", "build")
extlib_name = info.get_extlib_name();
if extlib_name is not None:
    info.set_extlib_info(
        f"./build/zig-windows-x64-Release/bin/lib{extlib_name}.dll",
        f"./build/zig-macos-aarch64-Release/lib/lib{extlib_name}.dylib",
        f"./build/zig-linux-x64-Release/lib/lib{extlib_name}.so",
        ""
    )
package_dir = info.project_root.joinpath("thunderstore_package")

def slugify(text: str) -> str:
    text = text.strip()
    text = re.sub(r'[\s_]+', '_', text)
    text = re.sub(r'[^a-zA-Z0-9_]', '', text)
    return text


def get_description() -> str:
    description = re.sub(r'[\n]+', ' ', info.mod_data["manifest"]["description"]).strip()
    if len(description) > 0 and len(description) <= 256:
        return description
    return info.mod_data["manifest"]["short_description"]


def get_website_url() -> str:
    if "website_url" in info.mod_data["manifest"]:
        return info.mod_data["manifest"]["website_url"]

    result = subprocess.run(
        [
            shutil.which("git"),
            "config",
            "--get",
            "remote.origin.url"
        ],
        cwd=info.project_root,
        capture_output=True,
        text=True
    )

    if result.returncode == 0:
        # print(f"Command output: {result.stdout}")
        return result.stdout.strip()
    else:
        # print(f"Command failed with error: {result.stderr}")
        return None


def get_package_manifest() ->dict[str, str]:
    return {
        "name":  slugify(info.mod_data["manifest"]["display_name"]),
        "version_number":  info.mod_data["manifest"]["version"],
        "website_url":  get_website_url(),
        "description":  get_description(),
        "dependencies":  []
    }


def create_package_directory():
    print(f"Creating package directory at '{package_dir}'...")
    os.makedirs(package_dir)


def create_manifest(path: Path) -> dict:
    manifest = get_package_manifest()

    print(f"Creating manifest at '{path}'...")
    path.write_text(json.dumps(manifest, indent=4))

    return manifest


def update_manifest(path: Path) -> dict:
    print(f"Updating manifest at '{path}'...")
    manifest = get_package_manifest();
    del manifest["name"]
    del manifest["dependencies"]

    current_manifest: dict[str, str] = json.loads(path.read_text());
    current_manifest.update(manifest)

    path.write_text(json.dumps(current_manifest, indent=4))

    return current_manifest


def create_readme(path: Path):
    print(f"Creating readme from description at '{path}'...")
    readme_str = f"# {info.mod_data['manifest']['display_name']}\n\n{info.mod_data['manifest']['description']}"
    path.write_text(readme_str)

def create_changelog(path: Path):
    print(f"Creating changlog at '{path}'...")
    readme_str = f"# CHANGELOG\n\n## Version 1.0.0\n\n* Initial release\n"
    path.write_text(readme_str)


def copy_icon(src_path: Path, dst_path: Path) -> bool:
    if (src_path.is_file()):
        print(f"Copying icon from '{src_path}' to '{dst_path}'...")
        shutil.copy(src_path, dst_path)
        return True
    else:
        print(f"No file '{src_path}' exists. You may need to create an icon manually...")
        return False


def copy_mod(src_path: Path, dst_path: Path) -> bool:
    if (src_path.is_file()):
        print(f"Copying mod from '{src_path}' to '{dst_path}'...")
        shutil.copy(src_path, dst_path)
        return True
    else:
        print(f"No file '{src_path}' exists. You need to build the mod first.")
        return False

def copy_extlib(src_path: Path, dst_path: Path) -> bool:
    if (src_path.is_file()):
        print(f"Copying extlib from '{src_path}' to '{dst_path}'...")
        shutil.copy(src_path, dst_path)
        return True
    else:
        print(f"No file '{src_path}' exists. You need to build the extlib first.")
        return False

def create_archive(package_dir: Path, dst_path: Path):
    new_zip = zipfile.ZipFile(dst_path, 'w')

    for i in os.listdir(package_dir):
        if i == "images":
            continue
        src_path = package_dir.joinpath(i)
        new_zip.write(src_path, i)

    new_zip.close()

def create_package():
    global extlib_name, info
    bm.run_build(["EXTLIB_CMAKE_PRESET_GROUP=Release"])

    fully_collected = True

    if not package_dir.is_dir():
        create_package_directory()

    manifest_file = package_dir.joinpath("manifest.json")
    if not manifest_file.is_file():
        manifest = create_manifest(manifest_file)
    else:
        manifest = update_manifest(manifest_file)

    readme_file = package_dir.joinpath("README.md")
    if not readme_file.is_file():
        create_readme(readme_file)

    changelog_file = package_dir.joinpath("CHANGELOG.md")
    if not changelog_file.is_file():
        create_changelog(changelog_file)

    icon_file = package_dir.joinpath("icon.png")
    if not icon_file.is_file():
        fully_collected = copy_icon(info.project_root.joinpath("thumb.png"), icon_file) and fully_collected

    mod_file = package_dir.joinpath(info.build_nrm_file.name)
    fully_collected = copy_mod(info.build_nrm_file, mod_file) and fully_collected

    if extlib_name is not None:
        dll_file = package_dir.joinpath(info.runtime_dll_file.name)
        dylib_file = package_dir.joinpath(info.runtime_dylib_file.name)
        so_file = package_dir.joinpath(info.runtime_so_file.name)
        fully_collected = copy_extlib(info.build_dll_file, dll_file) and fully_collected
        fully_collected = copy_extlib(info.build_dylib_file, dylib_file) and fully_collected
        fully_collected = copy_extlib(info.build_so_file, so_file) and fully_collected

    if fully_collected:
        print("Fully collected. Zipping mod package.")
        create_archive(package_dir, info.project_root.joinpath(f"{manifest['name']}.thunderstore.zip"))
    else:
        print("Files are missing.")


if __name__ == '__main__':
    create_package()
