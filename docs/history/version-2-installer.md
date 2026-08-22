# Version 2.0.2 — Clean Folder Selection Installer

Version 2.0.2 promotes the user-tested installer fix. The installer now starts with a blank destination field, so it no longer imports an outdated Rockstar registry path that could display duplicate `GTA IV` folder layers.

| Area | Version 2.0.2 behavior |
|---|---|
| Folder selection | The player selects the actual GTA IV folder once through **Browse**. |
| Path handling | The installer no longer reads Rockstar’s `InstallFolder` registry value. |
| Validation | Folder checks happen after **Install** is clicked, not while the destination field is being selected. |
| Unrecognized layouts | The installer shows the full selected path and lets the player return to the folder chooser instead of disabling the Install button. |
| Safety | The installer retains timestamped managed-file backups, the rollback path, and the Windows **Installed apps** uninstaller entry. |

The nine embedded configuration files match the repository’s current `tuned/` profile. The release contains only the installer, its auditable source, checksums, and configuration payload; it does not redistribute GTA IV, launcher files, saved games, FusionFix/DXVK binaries, or third-party mod assets.

> This release was published only after the revised folder-selection flow was successfully tested on Windows.
