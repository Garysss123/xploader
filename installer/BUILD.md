# Installer build dependencies

Build the helper with AArch64 GNU GCC 13 and **musl 1.2.5**, static, targeting
ARMv8-A without outlined LSE atomics. The musl release archive is
<https://musl.libc.org/releases/musl-1.2.5.tar.gz>, SHA256
`a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`.
Build/install musl with that cross compiler and `--target=aarch64-linux-gnu
--disable-shared --prefix=/your/musl/prefix`, using
`CFLAGS="-Os -march=armv8-a -mno-outline-atomics"`.

Run `bash tools/build_installer_guard.sh /your/musl/prefix`. Copy the resulting
helper and the accepted clean carrier to `installer/assets/` under the names
in `release.json`; verify the manifest's size/hash values before packaging.
Do not replace the accepted manifest merely to make an unrelated build pass.

The Windows executable was built with **Python 3.13.9**, the pinned
`build-requirements.txt`, and `tools/build_installer.ps1`. Use a separate venv
for these build dependencies. Python, OpenSSL, musl, GCC runtime and PyInstaller license notices
must accompany binary distribution. Platform Tools and original TWRP are
downloaded directly from their publishers, without bundling their binaries.

`installer/work` is a private recovery record, containing device identity,
boot and GPT bytes. Never include it in a public package or source commit.
