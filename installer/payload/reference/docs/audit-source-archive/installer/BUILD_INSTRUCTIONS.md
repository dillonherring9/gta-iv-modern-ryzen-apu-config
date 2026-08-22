# Turning this into a real `.exe`

I can't compile a `.exe` inside the sandbox this was built in — it has no network
access, so it can't download NSIS, Inno Setup, or `ps2exe`, and none of them are
preinstalled. `install.ps1` and `uninstall.ps1` already work today as-is (right-
click → *Run with PowerShell*, or `powershell -ExecutionPolicy Bypass -File
install.ps1`). Below are the two easiest ways to get an actual `.exe` on your own
machine, in order of how closely they match the original repo's installer.

## Route 1 — NSIS (matches the original repo's installer style)
1. Install NSIS (free, ~3 MB): https://nsis.sourceforge.io/Download
2. Keep this folder structure intact — `build_installer.nsi` expects
   `general-apu-edition/`, `radeon-660m-edition/`, and `MANUAL_INSTALLATION.md`
   one level up, alongside `install.ps1` / `uninstall.ps1` next to itself.
3. Create a one-line `LICENSE_NOTE.txt` one folder up (NSIS's license page needs
   a file to point at) — e.g. "Configuration files only. No GTA IV, FusionFix,
   or DXVK content included. See AUDIT_REPORT.md for full credits."
4. Right-click `build_installer.nsi` → **Compile NSIS Script**, or from a
   command prompt: `makensis build_installer.nsi`
5. `GTAIV_AfterDark_Setup.exe` is produced in the same folder. It requests
   admin (needed to set the "Run as administrator" compatibility flag, exactly
   like the original), then runs `install.ps1` for you.

## Route 2 — ps2exe (fastest, keeps everything in PowerShell)
1. From an elevated PowerShell: `Install-Module ps2exe -Scope CurrentUser`
2. `Invoke-ps2exe .\install.ps1 .\GTAIV_AfterDark_Setup.exe -requireAdmin -title "GTA IV After Dark Setup"`
3. Do the same for `uninstall.ps1` if you want a separate uninstaller `.exe`,
   or just keep `uninstall.ps1` as-is since it's already one right-click away.

Either route produces a real, double-clickable `.exe` with the exact same
validate → backup → apply → set-admin-flag → record behavior described in
`install.ps1`. Nothing about the underlying logic changes between "run the
`.ps1` directly" and "run the compiled `.exe`" — the compile step is purely
cosmetic/distribution convenience, so don't feel you have to do it before using
this.
