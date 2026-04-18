"""
ShareDir/data/skill_templates.csv 런타임 로더.

서버와 debug_tool 이 Single Source of Truth 를 공유한다:
  - sid, name, targeting, cooldown 등 모두 CSV 에서 읽는다.
  - debug_tool 전용 UX 메타(Q/W/E/R 입력 수집 방식)는 config.SKILL_INPUT_MODE_OVERRIDES 에서
    스킬 이름으로 덮어쓴다 (CSV 스키마를 서버와 공유하므로 클라 전용 컬럼은 두지 않는다).

주석행('#' 시작)은 스킵하고, 헤더 이후 데이터 행만 파싱한다.
"""

import csv
import os
from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class SkillTemplate:
    sid: int
    name: str
    targeting: str                # "Homing" | "Skillshot"
    projectile_speed: float
    projectile_radius: float
    projectile_range: float
    projectile_lifetime: float
    cooldown: float
    cost: int
    cast_range: float


_DEFAULT_CSV_PATH = os.path.normpath(os.path.join(
    os.path.dirname(__file__), os.pardir, "ShareDir", "data", "skill_templates.csv"))


class SkillTable:
    """스킬 메타 조회 테이블. PK = sid (CSV의 첫 컬럼)."""

    def __init__(self, by_name: dict[str, SkillTemplate]):
        self._by_sid: dict[int, SkillTemplate] = {t.sid: t for t in by_name.values()}
        self._by_name: dict[str, SkillTemplate] = dict(by_name)

    def get(self, sid: int) -> Optional[SkillTemplate]:
        """PK(sid) 로 조회. 기본 조회 경로."""
        return self._by_sid.get(sid)

    def get_by_name(self, name: str) -> Optional[SkillTemplate]:
        """name 은 unique 하지만 리네이밍 위험이 있어 디버깅/스크립팅 보조용."""
        return self._by_name.get(name)

    def sids(self) -> list[int]:
        return list(self._by_sid.keys())

    def names(self) -> list[str]:
        return list(self._by_name.keys())

    def __len__(self) -> int:
        return len(self._by_sid)

    def __contains__(self, sid: int) -> bool:
        return sid in self._by_sid


def load_from_csv(path: str = _DEFAULT_CSV_PATH) -> SkillTable:
    """CSV 를 읽어 SkillTable 을 반환한다.

    파일/컬럼 누락 시 예외를 올려 조용히 잘못된 기본값을 쓰지 않게 한다.
    """
    if not os.path.isfile(path):
        raise FileNotFoundError(f"skill_templates.csv not found at {path}")

    with open(path, encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]

    reader = csv.DictReader(lines)
    table: dict[str, SkillTemplate] = {}
    for row in reader:
        tpl = SkillTemplate(
            sid=int(row["sid"]),
            name=row["name"].strip(),
            targeting=row["targeting"].strip(),
            projectile_speed=float(row["projectile_speed"]),
            projectile_radius=float(row["projectile_radius"]),
            projectile_range=float(row["projectile_range"]),
            projectile_lifetime=float(row["projectile_lifetime"]),
            cooldown=float(row["cooldown"]),
            cost=int(row["cost"]),
            cast_range=float(row["cast_range"]),
        )
        if tpl.name in table:
            raise ValueError(f"duplicate skill name in CSV: {tpl.name}")
        table[tpl.name] = tpl
    return SkillTable(table)
