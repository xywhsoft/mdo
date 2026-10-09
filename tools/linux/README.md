# Linux release compiler baseline

The compiler root is Debian 10 (Buster), GCC 8, glibc 2.28. It is a build
environment, not a distribution installed on the user's machine. Ship only
the executable; users keep their own current GTK, WebKit and CA certificates.

`baseline.lock.json` pins the Python source and Debian archive public keyring.
The bundled `debian-archive-keyring.gpg` comes from Debian's signed
`debian-archive-keyring` package `2023.3+deb12u2`; it contains public signing
keys, including the Buster archive keys. Keeping this snapshot makes old
archive verification independent of whether the host removes obsolete keys.
It is used only for the dedicated historical build root. A second bootstrap
argument can select a different keyring explicitly.

According to the Debian package copyright file, the keys in these keyrings
are not subject to copyright; Debian support files are Copyright 2006 Michael
Vogt and licensed under GPL-2.0-or-later. Only public key material is bundled
here. The package and upstream source are
available at https://deb.debian.org/debian/pool/main/d/debian-archive-keyring/.

See `docs/linux.md` for complete build and runtime instructions. On Windows,
`--wsl --baseline-root PATH` invokes the prepared WSL root as its root user
for the workspace bind and chroot; it does not change the distro's default user.
