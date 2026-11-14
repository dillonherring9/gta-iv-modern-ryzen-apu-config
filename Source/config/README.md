# Configuration sources (checkpoint 269)

This authoring snapshot retains the **eight text files** that define the
checkpoint-245 model load order and radio station limit, unchanged through
checkpoint 269. Every byte matches the current playable release and the maintained live-file manifest. It includes
the two consolidated map IDEs and the save-numbering repair; it is not a
complete game overlay. Other files named by the DATs, models, audio, textures,
placement data and the owned GTA IV CE installation come from the playable
release and game. Do not install this folder as a standalone conversion.

The shared model list is expanded from `default.dat`, then `gta.dat`, with
`update/` precedence for `common:/` and `platform:/` paths. Peds count in that
same list. `hier` records do not count; an MLO contributes its header, not its
entity placements, rooms or portals. Repeated model definitions retain their
positions. Consolidating files must preserve the resulting ordered definitions.

The posted checkpoint-224 build defines the first **5,740** entries. Checkpoint
245 preserves that complete prefix and appends **F_Y_VCDANCER_07–14** as entries
5,740–5,747 (zero based), in a `peds` block at the end of the last loaded IDE,
`vc_models.ide`. Their earlier placement in `peds.ide` shifted old-save pickup
models. Future model definitions must be appended after the existing list;
inserting, deleting or reordering earlier definitions can change saved model
references even when every model remains present. The owner tested checkpoint
245 with an existing save: the Ocean View hotel shirt pickup is correct and all
Pole Position dancers are present. This is a specific gameplay confirmation,
not every-save or full-playthrough coverage.

The checkpoint-241 radio change broke existing saves and was undone. The
current `stationslimit.txt` is **10**, byte-identical to the posted 224 version.
The current `GAME.DAT16` retains the original radio object numbering and the
two appended vehicle-sound clones from step 233. Its expected raw identity is
recorded in [manifest.json](manifest.json); the binary belongs to the playable
release and is deliberately absent here. Do not reapply checkpoint 241's
nine-station GAME data or station limit.

From the Source folder, run the read-only, standard-library checker:

```sh
python3 -I tools/check_model_order.py
python3 -I tools/check_model_order.py --root "/path/to/release" --report ../VCANGCE-build/model-order.json
```

The default checks this snapshot. `--root` checks an extracted playable release
or installed game, including the expected `GAME.DAT16` hash. Both modes compare
the full ordered model list, all eight text identities, the five IDE loads and
selected pickup model positions against the frozen checkpoint-224 prefix plus
the eight new peds. JSON is printed to standard output; `--report` additionally
writes a new file outside Source and the input root. No game or save is written.

[model-order-224.json](model-order-224.json) records every `(section, name)`
entry in that frozen prefix, plus the identities of the eight unchanged CE base
text files used beneath the old release overlay. The listed indices are
zero-based model-list positions. The checker does not parse saves or assert a
single universal conversion between those positions and all save-record fields.
After intentional authoring changes, retain the old prefix and review any
updated checkpoint manifest separately; the supplied checker verifies this
published snapshot exactly.

## Packaged runtime defaults

[runtime-defaults.json](runtime-defaults.json) separately indexes all **12**
packaged `.ini`, `.cfg` and `.conf` files, including `VCRender.ini`, the ASI
loader's `global.ini`, private DXVK settings and the paired upscaler configuration.
These bytes come from the frozen Build269 release, including clean controller
speaker defaults, rather than personal live settings. The model-order checker
continues to validate its eight model/radio text files; runtime defaults are
covered by the overall source checksum manifest and the separate defaults index.
With VCRender active, the in-game DirectX/Vulkan selector chooses only the
fallback; the package keeps `API = 1`.
