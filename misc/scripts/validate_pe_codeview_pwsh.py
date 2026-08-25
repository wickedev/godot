"""Run the workflow's own PowerShell PE parser against the shared fixtures.

validate_pe_codeview.py tests a Python mirror of the parse. That mirror cannot catch
PowerShell-specific defects, and one bit immediately: `$b[$a..$b]` on a byte array
yields Object[], which does not bind the Guid(byte[]) overload, so PowerShell selected
Guid(string) and threw "Guid should contain 32 digits" on a perfectly valid PE. The
Python mirror was green throughout.

So this script extracts Get-PeCodeView verbatim from
.github/workflows/release_symbols.yml, generates the same fixtures, and runs the real
thing. It needs pwsh; where pwsh is absent it skips rather than fails, because the
Windows runner is where it counts and not every dev box has PowerShell.

Run: python misc/scripts/validate_pe_codeview_pwsh.py    (exits non-zero on failure)
"""

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WORKFLOW = os.path.join(REPO, ".github", "workflows", "release_symbols.yml")

EXPECT_OK = ["ok_pe32plus", "ok_pe32", "ok_notfirst", "ok_age9"]


def load_fixture_builder():
    spec = importlib.util.spec_from_file_location("v", os.path.join(HERE, "validate_pe_codeview.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def extract_function(name="Get-PeCodeView"):
    with open(WORKFLOW, encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    src = next(st["run"] for st in doc["jobs"]["build"]["steps"] if st.get("shell") == "pwsh")
    lines = src.split("\n")
    start = next(i for i, line in enumerate(lines) if line.startswith(f"function {name}"))
    end = next(i for i in range(start + 1, len(lines)) if lines[i] == "}")
    return "\n".join(lines[start : end + 1])


HARNESS = """

$expectOk = @({expect})
$fail = 0
foreach ($f in (Get-ChildItem {fixtures}/*.bin | Sort-Object Name)) {{
  $name = $f.BaseName
  $shouldPass = $expectOk -contains $name
  try {{
    $cv = Get-PeCodeView $f.FullName
    $id = ($cv.Guid.ToString("N") + $cv.Age.ToString("x")).ToUpper()
    if ($shouldPass) {{ Write-Output "[OK  ] $name -> $id pdb=$($cv.Pdb)" }}
    else {{ Write-Output "[FAIL] $name ACCEPTED malformed -> $id"; $fail++ }}
  }} catch {{
    if ($shouldPass) {{ Write-Output "[FAIL] $name REJECTED valid PE: $($_.Exception.Message)"; $fail++ }}
    else {{ Write-Output "[OK  ] $name rejected: $($_.Exception.Message)" }}
  }}
}}
Write-Output "FAILURES=$fail"
"""


def main():
    pwsh = shutil.which("pwsh") or shutil.which("powershell")
    if not pwsh:
        print("pwsh not found; skipping. The Windows runner is where this must pass.")
        return 0

    v = load_fixture_builder()
    fn = extract_function()
    if "[byte[]]" not in fn:
        print("[FAIL] extracted Get-PeCodeView has no [byte[]] cast on the GUID slice.")
        print("       Without it PowerShell binds Guid(string) and rejects valid PEs.")
        return 1

    tmp = tempfile.mkdtemp(prefix="pecv_")
    fixtures = os.path.join(tmp, "fixtures")
    os.makedirs(fixtures)
    cases = {
        "ok_pe32plus": v.build_pe(magic=v.PE32_PLUS),
        "ok_pe32": v.build_pe(magic=v.PE32),
        "ok_notfirst": v.build_pe(extra_entries=2),
        "ok_age9": v.build_pe(age=9),
        "bad_size0": v.build_pe(size_of_data=0),
        "bad_trunc": v.build_pe(size_of_data=20),
        "bad_noterm": v.build_pe(pdb=b"godot.pdb", terminate_name=False, size_of_data=25),
        "bad_notrsds": v.build_pe(signature=b"NB10"),
        "bad_dirsize": v.build_pe(dir_size_override=30),
        "bad_dirrva": v.build_pe(dir_rva_override=0x900000),
    }
    for key, image in cases.items():
        with open(os.path.join(fixtures, f"{key}.bin"), "wb") as f:
            f.write(image)

    script = os.path.join(tmp, "test.ps1")
    expect = ",".join(f'"{n}"' for n in EXPECT_OK)
    with open(script, "w", encoding="utf-8") as f:
        f.write(fn + HARNESS.format(expect=expect, fixtures=fixtures))

    result = subprocess.run([pwsh, "-NoProfile", "-File", script], capture_output=True, text=True)
    print(result.stdout.strip())
    if result.stderr.strip():
        print(result.stderr.strip())
    if "FAILURES=0" not in result.stdout:
        print("\nPowerShell parse failed on at least one fixture.")
        return 1
    print("\nWorkflow PowerShell parser passed all fixtures.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
