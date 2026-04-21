"""
ShareDir/data/monster_templates.csv 런타임 로더.

서버는 MonsterInfo.tid 만 내려준다 (이름은 서버가 소유하지 않음). 클라는 여기서 tid 로
MonsterTemplate 을 조회해 name/스프라이트 선택 근거를 얻는다 — skill_data.py 와 동일 패턴.

주석행('#' 시작) 은 스킵하고 헤더 이후 데이터 행만 파싱한다.
"""

import csv
import os
from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class MonsterTemplate:
    tid: int
    name: str
    hp: int
    max_hp: int
    detect_range: float
    leash_range: float
    move_speed: float


_DEFAULT_CSV_PATH = os.path.normpath(os.path.join(
    os.path.dirname(__file__), os.pardir, "ShareDir", "data", "monster_templates.csv"))


class MonsterTable:
    """몬스터 메타 조회 테이블. PK = tid (CSV 의 첫 컬럼)."""

    def __init__(self, rows: list[MonsterTemplate]):
        self._by_tid: dict[int, MonsterTemplate] = {t.tid: t for t in rows}

    def get(self, tid: int) -> Optional[MonsterTemplate]:
        return self._by_tid.get(tid)

    def name(self, tid: int) -> str:
        """tid 에 해당하는 이름. 테이블 미스 시 빈 문자열 — 렌더러의 substring 매칭이 generic fallback 으로 귀결."""
        t = self._by_tid.get(tid)
        return t.name if t else ""

    def __len__(self) -> int:
        return len(self._by_tid)

    def __contains__(self, tid: int) -> bool:
        return tid in self._by_tid


def load_from_csv(path: str = _DEFAULT_CSV_PATH) -> MonsterTable:
    """CSV 를 읽어 MonsterTable 반환. 파일 누락 시 예외."""
    if not os.path.isfile(path):
        raise FileNotFoundError(f"monster_templates.csv not found at {path}")

    with open(path, encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]

    reader = csv.DictReader(lines)
    rows: list[MonsterTemplate] = []
    for row in reader:
        rows.append(MonsterTemplate(
            tid=int(row["tid"]),
            name=row["name"].strip(),
            hp=int(row["hp"]),
            max_hp=int(row["maxHp"]),
            detect_range=float(row["detectRange"]),
            leash_range=float(row["leashRange"]),
            move_speed=float(row["moveSpeed"]),
        ))
    return MonsterTable(rows)
