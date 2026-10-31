# Third-Party Notices

dreamkurumiHT is not affiliated with, endorsed by or sponsored by Keen Games GmbH. *Enshrouded* and
related names are trademarks of their owners and are used only to identify the game this software
interoperates with. This repository contains no game assets or game code.

The framework's own license is stated in `LICENSE` (to be chosen; see the licensing notes in the project
discussion). The components below keep their own licenses.

> **How this file was produced.** Components marked *vendored* were found in the `include/` tree reviewed
> for v3.8.3; their copyright lines below are copied from the headers of the vendored files. Components
> marked *if bundled* were NOT in that tree: keep the entry only if you actually ship the code or binary,
> and replace the `<...>` placeholders with the exact lines of the copy you ship. A notice must travel
> with every binary release too (`THIRD_PARTY_LICENSES.md` goes into each GitHub Release archive).

## Summary

| Component | Version / source | License (SPDX) | Status |
|---|---|---|---|
| LuaJIT | 2.1 (`lua/luajit.h`, "LuaJIT 2.1.1788856981") | MIT | vendored headers; add the runtime if you ship it |
| Lua 5.1 API headers | 5.1.4 (`lua/lua.h`, shipped with LuaJIT) | MIT | vendored |
| yyjson | `yyjson/yyjson.[ch]` | MIT | vendored |
| Hacker Disassembler Engine 64 (HDE64) | `hde/hde64.[ch]`, `hde/table64.h` | BSD-2-Clause **(provenance to verify, see below)** | vendored |
| Dear ImGui | `<version>` | MIT | if bundled |
| Vulkan-Headers | `<version>` | Apache-2.0 OR MIT (choose MIT here) | if bundled |
| GameNetworkingSockets | `<version>` | BSD-3-Clause | only if code or headers are copied |
| Zydis | planned for v4.0 | MIT | not yet used |

The Vulkan loader (`vulkan-1.dll`) and the game's own libraries are loaded from the user's system and are
not redistributed.

---

## LuaJIT (vendored headers: `lua/luajit.h`, `lua/lua.hpp`, ...)

Copyright (C) 2005-2026 Mike Pall. All rights reserved. (as printed in `luajit.h`; use the range of the copy you ship)

```text
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```

If you ship the LuaJIT runtime (`lua51.dll` / `luajit.dll`), this notice must accompany that binary.
LuaJIT also contains third-party code under its own notices (see the `COPYRIGHT` file of the LuaJIT
source distribution you build from): include that file's text when you distribute the runtime.

## Lua 5.1 API headers (`lua/lua.h`, `lauxlib.h`, `lualib.h`, `luaconf.h`)

Copyright (C) 1994-2008 Lua.org, PUC-Rio. All rights reserved. (as printed in `lua/lua.h`)

```text
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```


## yyjson (`yyjson/yyjson.h`, `yyjson/yyjson.c`)

Copyright (c) 2020 YaoYuan <ibireme@gmail.com>

```text
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```


## Hacker Disassembler Engine 64 C (`hde/hde64.c`, `hde/hde64.h`, `hde/table64.h`)

Copyright (c) 2008-2009, Vyacheslav Patkov. All rights reserved.

> **Verify before release.** The vendored source files carry only the line above ("All rights reserved")
> and no permission grant. The BSD-2-Clause terms below are the ones under which MinHook redistributes this
> exact code (listed in MinHook's `LICENSE.txt`, section "Hacker Disassembler Engine 32/64 C"). Confirm that
> your copy came from MinHook (the byte-order mark and the "hde64.h: C/C++ header file" banner suggest it did),
> copy the exact text from that file, and keep the upstream `LICENSE.txt` section in your repository.
> If you cannot establish where the files came from, do not publish them: replace the decoder
> (planned: Zydis, MIT) before the first public release.

```text
Redistribution and use in source and binary forms, with or without modification, are permitted provided
that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
```


## Dear ImGui (if bundled)

Copyright (c) 2014-`<year>` Omar Cornut  (use the line from the `LICENSE.txt` of the version you ship)

```text
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```


## Vulkan-Headers (if bundled)

Copyright `<years>` The Khronos Group Inc.  (use the line from the file header of the version you ship)

Vulkan-Headers is, to my knowledge, offered under `Apache-2.0 OR MIT` (confirm in the `LICENSE.md` of the version
you ship); this project would use it under the MIT option:

```text
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```


## GameNetworkingSockets (only if you copy its code or headers)

Copyright (c) `<year>`, Valve Corporation. All rights reserved.  (use the exact line from the `LICENSE` you copied)

```text
Redistribution and use in source and binary forms, with or without modification, are permitted provided
that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
   promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
```

Hooking the game's own, already-loaded GameNetworkingSockets functions by address or by exported name does
not copy Valve's code and needs no notice; copying its headers or sources does.

## Zydis (planned for v4.0)

Copyright `<years and holders exactly as in the LICENSE of the release you vendor>`. MIT License: when it is added,
paste that notice and list its `zycore-c` dependency the same way.

---

## Maintenance checklist

- Add an entry in the same commit that adds a dependency; CI should fail if `include/**/third_party` or a
  `vendor/` directory changes without touching this file.
- Keep the license text byte-for-byte from the version you ship; years and holder names change between releases.
- Ship this file inside every release archive next to the DLLs, and show a one-line pointer in `README.md`.
