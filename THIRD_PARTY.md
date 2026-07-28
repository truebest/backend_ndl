# Third-party components

All source code in this subproject is original MIT-licensed code of this
repository. Host tests use the system-provided GoogleTest framework under its
BSD 3-Clause license; GoogleTest is not vendored, installed, or linked into
the production library.

Both DirectMedia compatibility profiles use minimal independently written
private ABI declarations. No official or community/SS4S header is copied,
installed, shipped, or included in the public API.

An optional host-only conformance test may include `NDL/NDL_directmedia.h`
from a separately installed official webOS target NDK. The NDK header is used
only as an ABI reference and remains subject to the terms shipped with that
NDK; it is never an input to the production library or package.

At runtime the wrapper dlopens the `libNDL_directmedia.so.1` facade supplied by
webOS. The facade owns loading `libNDL_directmedia_impl.so.1`,
`libplayerAPIs.so.1`, and other system dependencies. No webOS system source or
binary is copied, linked, or redistributed by `backend_ndl`.
