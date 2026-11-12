# Opus 1.6 build subset

This is a curated copy of the retained official Opus 1.6 source, not the whole
upstream distribution. It includes the 137 C translation units used by this
project's floating-point, no-SIMD / no-runtime-CPU-dispatch Windows x86 build,
all upstream headers, the three source-list make fragments, upstream README,
COPYING and LICENSE_PLEASE_READ.txt. Original file bytes are unchanged.

The portable `tools/build_native.py` compiles this subset directly. Opus is
statically linked into `VCScriptHook.asi`; no separate Opus DLL is required.

Upstream: <https://github.com/xiph/opus/tree/v1.6>

Retained source archive SHA256:
`a9f9f04196174e4a1bdb5e0c5a85e36ebbd45a18cbbbb8be5024c59b3c001964`.

See COPYING for copyright, BSD redistribution conditions and warranty terms,
and LICENSE_PLEASE_READ.txt for the contributor and patent references.
