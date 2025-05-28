import pathlib, subprocess, os, shutil, tomllib, zipfile, sys, json
from pathlib import Path


class ModInfo:
    project_root: Path
    mod_toml_file: Path
    mod_data: dict
    
    def __init__(self, mod_toml_str: str, build_dir: str, windows_lib: str, macos_lib: str, linux_lib: str, native_lib: str):
        self.project_root = Path(__file__).parent
        self.mod_toml_file = self.project_root.joinpath(mod_toml_str)
        
        self.mod_data = tomllib.loads(self.mod_toml_file.read_text())
        # print(mod_data)
        self.build_dir = self.project_root.joinpath(build_dir)
        self.build_nrm_file = self.build_dir.joinpath(f"{self.mod_data['inputs']['mod_filename']}.nrm")
        
        self.runtime_dir = self.project_root.joinpath("runtime")
        self.runtime_mods_dir = self.runtime_dir.joinpath("mods")
        self.runtime_nrm_file = self.runtime_mods_dir.joinpath(f"{self.mod_data['inputs']['mod_filename']}.nrm")
        
        self.build_dll_file = self.project_root.joinpath(windows_lib)
        self.build_pdb_file = self.build_dll_file.with_suffix(".pdb")
        self.build_dylib_file = self.project_root.joinpath(macos_lib)
        self.build_so_file = self.project_root.joinpath(linux_lib)
        self.build_native_file = self.project_root.joinpath(native_lib)
        self.build_native_pdb_file = self.build_native_file.with_suffix(".pdb")
        
        self.runtime_dll_file = self.runtime_mods_dir.joinpath(self.build_dll_file.name.removeprefix("lib"))
        self.runtime_pdb_file = self.runtime_mods_dir.joinpath(self.build_pdb_file.name.removeprefix("lib"))
        self.runtime_dylib_file = self.runtime_mods_dir.joinpath(self.build_dylib_file.name.removeprefix("lib"))
        self.runtime_so_file = self.runtime_mods_dir.joinpath(self.build_so_file.name.removeprefix("lib"))
        self.runtime_native_file = self.runtime_mods_dir.joinpath(self.build_native_file.name.removeprefix("lib"))
        self.runtime_native_pdb_file = self.runtime_mods_dir.joinpath(self.build_native_pdb_file.name.removeprefix("lib"))
        
        self.assets_archive_path =self.project_root.joinpath("assets_archive.zip")
        
        # Handle recomp compilers:
        self.recomp_user_compilers_path = self.project_root.joinpath("./recomp_user_compilers.json")
        self.recomp_compiler_info = {}
        if not self.recomp_user_compilers_path.exists():
            self.create_user_mod_compilers_json()
        
        else:
            self.recomp_compiler_info = json.loads(self.recomp_user_compilers_path.read_text())
            
    def create_user_mod_compilers_json(self):
        self.recomp_compiler_info = {
            "mod_compiling": {
                "compiler": "clang",
                "linker": "ld.lld"
            }
        }
        self.recomp_user_compilers_path.write_text(json.dumps(self.recomp_compiler_info, indent=4))
    
    def get_mod_file(self):
        name = f"{self.mod_data['inputs']['mod_filename']}.nrm"
        print(self.build_dir.joinpath(name))
    
    def get_mod_elf(self):
        print(self.mod_toml_file.parent.joinpath(self.mod_data['inputs']['elf_path']))
        
    def get_mod_compiler(self):
        print(self.recomp_compiler_info["mod_compiling"]["compiler"])
        
    def get_mod_linker(self):
        print(self.recomp_compiler_info["mod_compiling"]["linker"])

    def create_asset_archive(self, assets_extract_path_str: str):
            assets_extract_path = self.project_root.joinpath(assets_extract_path_str)
            print(f"Assets folder '{assets_extract_path.name}' not found. Extracting assets from '{self.assets_archive_path.name}'...")
            with zipfile.ZipFile(self.assets_archive_path, 'r') as zip_ref:
                zip_ref.extractall(assets_extract_path)

    def copy_to_runtime_dir(self):

        # Copying files for debugging:
        os.makedirs(self.runtime_mods_dir, exist_ok=True)
        portable_txt = self.runtime_dir.joinpath("portable.txt")
        if not portable_txt.exists():
            portable_txt.write_text("")
            print(f"Created '{portable_txt}'.")
        
        self.copy_if_exists(self.build_nrm_file, self.runtime_nrm_file)
        self.copy_if_exists(self.build_dll_file, self.runtime_dll_file)
        self.copy_if_exists(self.build_pdb_file, self.runtime_pdb_file)
        self.copy_if_exists(self.build_dylib_file, self.runtime_dylib_file)
        self.copy_if_exists(self.build_so_file, self.runtime_so_file)
        self.copy_if_exists(self.build_native_file, self.runtime_native_file)
        self.copy_if_exists(self.build_native_pdb_file, self.runtime_native_pdb_file)

    def copy_if_exists(self, src: Path, dest: Path):
        if src.exists():
            shutil.copy(src, dest)
            print(f"'{src}' copied to '{dest}'.")
        else:
            print(f"'{src}' does not exist. Skipping.")

    def run_clean(self):
        shutil.rmtree(self.build_dir)
        shutil.rmtree(self.project_root.joinpath("./N64Recomp/build"))


def run_build(args: list[str]):
    make_run = subprocess.run(
        [
            shutil.which("make"),
        ] + args,
        cwd=pathlib.Path(__file__).parent
        
    )
    if make_run.returncode != 0:
        raise RuntimeError("Make failed!")

if __name__ == '__main__':
    run_build(sys.argv[1:])
