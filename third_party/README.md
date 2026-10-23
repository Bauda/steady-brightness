# Vendored NVIDIA headers

`nvapi/` contains the eight public NVIDIA headers required by this project, copied unchanged from [NVIDIA/nvapi commit 70d337db9186e968eab622f7e786de7e437faf3d](https://github.com/NVIDIA/nvapi/tree/70d337db9186e968eab622f7e786de7e437faf3d). Their Git blob hashes were compared against that upstream tree before the first public release.

License: MIT. Each header retains its upstream copyright and license notice. See [THIRD-PARTY-NOTICES.txt](../THIRD-PARTY-NOTICES.txt). No driver DLL or binary NVAPI SDK library is bundled.

To update, choose an explicit upstream commit, copy the required headers, verify the notices and includes, record the new commit here, rebuild, and test the affected fallback on suitable hardware. Do not automatically track the upstream main branch during builds.
