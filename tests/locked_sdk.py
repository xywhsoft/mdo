"""Path of the single explicitly locked SDK export, without duplicating it."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sdk_root():return ROOT/'.build/sdk-overlays'/json.loads((ROOT/'deps.lock').read_text(encoding='utf-8'))['sdk_overlay']['sha256']
