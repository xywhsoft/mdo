"""Prepare the embedded Windows distribution for normal Python CLI use."""
from pathlib import Path


def prepare_windows_python(directory: Path) -> None:
    """Only transform a disposable/staged runtime, before computing its manifest.

    CPython finds its bundled stdlib ZIP and extension modules beside python.exe.
    The upstream ._pth instead forces isolation even without -I, hiding project
    imports and PYTHONPATH. Removing it restores native CLI path initialization;
    explicit -I, -E and -P retain their standard behavior. No launcher is needed.
    """
    if not (directory / "python.exe").is_file():
        raise FileNotFoundError(directory / "python.exe")
    if not list(directory.glob("python3*.zip")):
        raise FileNotFoundError("Bundled Python standard library ZIP is missing")
    for path in directory.glob("python*._pth"):
        path.unlink()
