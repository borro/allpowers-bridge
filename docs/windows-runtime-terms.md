# Microsoft runtime components

Copyright (c) Microsoft Corporation. All rights reserved.

The Windows EXEs contain unmodified Microsoft C/C++ runtime code linked in
Release mode with `/MT`. This code is not relicensed under the application's
MIT license. The Microsoft C++ Standard Library's open-source portions retain
Apache-2.0 WITH LLVM-exception; see `MSVC-STL.md` in the embedded notices.

Microsoft Distributable Code is supplied as part of this application, not as a
standalone runtime SDK. Distributors may pass on these unmodified components
as part of the application, retaining the applicable notices and terms; this
does not grant a right to extract and redistribute Microsoft development
libraries, compiler tools, or their source code. Do not remove Microsoft notices
or claim Microsoft endorsement. Use is subject to applicable law. No Microsoft
warranty or support is provided by this project.

Restrictions in Microsoft's terms on modification, disassembly or reverse
engineering apply to Microsoft code only, subject to applicable law and the
exceptions required by governing open-source licenses. They do not prohibit
modifying/relinking Qt, reverse engineering to debug those modifications, or
running the resulting application. The application's MIT grant and each
open-source component's own terms remain in effect for that component.

Publishers must hold a valid Visual Studio license permitting their use of the
tools and redistribution of the runtime. Community is available without charge
for qualifying individual and open-source development; a Build Tools installation
by itself is not proof of that license. End users do not need Visual Studio to
run the application. Use released toolchains and Release runtimes; do not ship
Debug or preview runtime components or standalone Microsoft `.lib` files.

Authoritative terms and distribution guidance:

- https://visualstudio.microsoft.com/license-terms/vs2026-ga-community/
- https://visualstudio.microsoft.com/license-terms/vs2026-ga-diagnostic-buildtools/
- https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution
- https://www.microsoft.com/licensing/guidance/Visual-Studio
- https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features

These component notices do not grant rights beyond the applicable upstream
licenses or replace a publisher's obligations under its Microsoft agreement.
