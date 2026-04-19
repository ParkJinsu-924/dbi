"""
ShareDir/data/effects.csv 런타임 로더.

서버와 debug_tool 이 동일한 effects 테이블을 공유한다. 서버가 S_BuffApplied(eid,...) 를
보내면 클라가 eid 로 여기서 stat/magnitude/is_percent 를 조회해 로컬 prediction (예: 이동속도)
계산에 반영한다.

주석행('#') 스킵, 헤더 후 데이터 행만 파싱.
"""

import csv
import os
from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class EffectDef:
    eid: int
    type: str             # "Damage" | "Heal" | "StatMod" | "CCState" | ...
    magnitude: float
    duration: float
    scaling_ad: float
    scaling_ap: float
    stat: str             # "None" | "MoveSpeed" | "AttackSpeed" | "MaxHp" | "Damage"
    is_percent: bool
    cc_flag: str          # "None" | "Stun" | "Silence" | "Root" | "Slow" | "Invulnerable"


_DEFAULT_CSV_PATH = os.path.normpath(os.path.join(
    os.path.dirname(__file__), os.pardir, "ShareDir", "data", "effects.csv"))


class EffectTable:
    def __init__(self, by_eid: dict[int, EffectDef]):
        self._by_eid = dict(by_eid)

    def get(self, eid: int) -> Optional[EffectDef]:
        return self._by_eid.get(eid)

    def __contains__(self, eid: int) -> bool:
        return eid in self._by_eid

    def __len__(self) -> int:
        return len(self._by_eid)


def _parse_bool(s: str) -> bool:
    return s.strip().lower() in ("1", "true", "yes", "y")


def load_from_csv(path: str = _DEFAULT_CSV_PATH) -> EffectTable:
    if not os.path.isfile(path):
        raise FileNotFoundError(f"effects.csv not found at {path}")

    with open(path, encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]

    reader = csv.DictReader(lines)
    table: dict[int, EffectDef] = {}
    for row in reader:
        e = EffectDef(
            eid=int(row["eid"]),
            type=row["type"].strip(),
            magnitude=float(row["magnitude"]),
            duration=float(row["duration"]),
            scaling_ad=float(row["scaling_ad"]),
            scaling_ap=float(row["scaling_ap"]),
            stat=row.get("stat", "None").strip() or "None",
            is_percent=_parse_bool(row.get("is_percent", "false")),
            cc_flag=row.get("cc_flag", "None").strip() or "None",
        )
        if e.eid in table:
            raise ValueError(f"duplicate effect eid in CSV: {e.eid}")
        table[e.eid] = e
    return EffectTable(table)
