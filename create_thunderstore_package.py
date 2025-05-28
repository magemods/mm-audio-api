import subprocess, os, shutil, json, zipfile
from pathlib import Path
import make_python_functions as bm

info = bm.ModInfo("./mod.toml", "build")
package_dir = info.project_root.joinpath("thunderstore_package")

def get_git_url() -> str:
    result = subprocess.run(
        [
            shutil.which("git"),
            "config", 
            "--get", 
            "remote.origin.url"
        ],
        cwd=bm.project_root,
        capture_output=True,
        text=True
    )

    if result.returncode == 0:
        # print(f"Command output: {result.stdout}")
        return result.stdout.strip()
    else:
        # print(f"Command failed with error: {result.stderr}")
        return None

def get_default_package_manifest() ->dict[str, str]:
    return {
        "name":  bm.mod_data["manifest"]["id"],
        "version_number":  bm.mod_data["manifest"]["version"],
        "website_url":  get_git_url(),
        "description":  bm.mod_data["manifest"]["short_description"],
        "dependencies":  []
    }


def create_package_directory():
    print(f"Creating package directory at '{package_dir}'...")
    os.makedirs(package_dir)


def create_manifest(path: Path):
    print(f"Creating manifest at '{path}'...")
    path.write_text(json.dumps(get_default_package_manifest(), indent=4))
   
 
def update_manifest(path: Path):
    print(f"Updating manifest at '{path}'...")
    current_manifest: dict[str, str] = json.loads(path.read_text());
    current_manifest.update({
        "name":  info.mod_data["manifest"]["id"],
        "version_number":  info.mod_data["manifest"]["version"],
        "description":  info.mod_data["manifest"]["short_description"],
    })
    path.write_text(json.dumps(current_manifest, indent=4))


def create_readme(path: Path):
    print(f"Creating readme at from description at '{path}'...")
    readme_str = f"# {info.mod_data['manifest']['display_name']}\n\n{info.mod_data['manifest']['description']}"
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

def create_archive(package_dir: Path, dst_path: Path):
    new_zip = zipfile.ZipFile(dst_path, 'w')
    
    for i in os.listdir(package_dir):
        src_path = package_dir.joinpath(i)
        new_zip.write(src_path, i)
        
    new_zip.close()

def create_package():
    info.run_build()
    
    fully_collected = True
    
    if not package_dir.is_dir():
        create_package_directory()
        
    manifest_file = package_dir.joinpath("manifest.json")
    if not manifest_file.is_file():
        create_manifest(manifest_file)
    else: 
        update_manifest(manifest_file)
        
    readme_file = package_dir.joinpath("README.md")
    if not readme_file.is_file():
        create_readme(readme_file)
    
    icon_file = package_dir.joinpath("icon.png")
    if not icon_file.is_file():
        fully_collected = fully_collected and copy_icon(info.project_root.joinpath("thumb.png"), icon_file)
    
    mod_file = package_dir.joinpath(info.build_nrm_file.name)
    fully_collected = fully_collected and copy_mod(info.build_nrm_file, mod_file)
    
    if fully_collected:
        print("Fully collected. Zipping mod package.")
        create_archive(package_dir, info.project_root.joinpath(f"{info.mod_data['inputs']['mod_filename']}.thunderstore.zip"))
    else:
        print("Files are missing.")
    
        
        

if __name__ == '__main__':
    create_package()

