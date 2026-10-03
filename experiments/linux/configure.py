#!/usr/bin/env python3
"""Create a credential-free, fingerprinted configuration after successful builds."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    package = root / "archives/uautomizer-official.zip"
    with package.open("rb") as stream:
        if hashlib.file_digest(stream, "md5").hexdigest() != "03755e11edbea9efb6de954970f014a6":
            raise RuntimeError("Automizer archive does not match published checksum")
    common_java = {"_JAVA_OPTIONS": "-Xmx768m -Xss4m"}
    config = {"dataset": str(root / "svcomp2026-final-assertions"),
              "veriabs_status": "HELD: user requested permission review; do not execute",
              "arithexe": {"executable": str(root / "ArithExe/build/arith_exe"),
                           "environment": {"ARITHEXE_SOLVER_PYTHON": str(root / "venv/bin/python"),
                                           "ARITHEXE_SOLVER_WORKER": str(root / "ArithExe/build/solver_worker.py"),
                                           "ARITHEXE_CLANG": "/usr/bin/clang-20",
                                           "ARITHEXE_FORCE_PATH_EXPRESSIONS": "1",
                                           "ARITHEXE_PATH_MAX_ROOT": "6", "ARITHEXE_PATH_BEAM": "4",
                                           "ARITHEXE_PATH_EVIDENCE": "2"}},
              "icra": {"executable": str(root / "icra/icra"), "loadpath": str(root / "icra/duet/lib"),
                       "environment": dict(common_java,
                           PATH="/usr/lib/jvm/java-11-openjdk-amd64/bin:/usr/bin:/bin",
                           LD_LIBRARY_PATH=f"{root}/venv/lib/python3.14/site-packages/z3/lib:{root}/opam/icra-4.14/lib/stublibs")},
              "automizer": {"executable": str(root / "tools/UAutomizer-linux/Ultimate.py"),
                            "options": ["--full-output"],
                            "environment": dict(common_java, PATH="/usr/lib/jvm/java-21-openjdk-amd64/bin:/usr/bin:/bin")}}
    for name in ["arithexe", "icra", "automizer"]:
        executable = Path(config[name]["executable"])
        if not executable.is_file():
            raise RuntimeError("Missing executable: " + str(executable))
        config[name]["executable_sha256"] = sha(executable)
    config["automizer"]["archive_sha256"] = sha(package)
    config["icra"]["base_patch_sha256"] = sha(root / "archives/icra-linux-base.patch")
    config["icra"]["upstream_revision"] = "ee3fd360ee75490277dd3fd05d92e1548db983e4"
    snapshots = [root / "archives/arithexe-ready.tar.gz", root / "archives/icra-source.tar.gz",
                 root / "archives/icra-z3-source.tar.gz", root / "archives/path-expression-linux-materials.tar.gz",
                 root / "archives/path-expression-build-updates.tar.gz"]
    config["source_archive_sha256"] = {p.name: sha(p) for p in snapshots}
    (root / "configuration.json").write_text(json.dumps(config, indent=2) + "\n")
    subprocess.run([str(root / "venv/bin/pip"), "freeze"], stdout=(root / "logs/python-inventory.txt").open("w"), check=True)
    subprocess.run(["dpkg-query", "-W"], stdout=(root / "logs/ubuntu-package-inventory.txt").open("w"), check=True)
    subprocess.run(["lscpu"], stdout=(root / "logs/cpu-inventory.txt").open("w"), check=True)
    print("Prepared configuration for three tools; VeriAbs remains held.")


if __name__ == "__main__":
    main()
