"""skill_data.py — ShareDir CSV 로드/조회/오류 처리 테스트."""

import os
import textwrap

import pytest

import skill_data


def test_loads_default_csv():
    """배포된 ShareDir/data/skill_templates.csv 를 실제로 읽을 수 있어야 한다."""
    table = skill_data.load_from_csv()
    assert len(table) >= 4   # auto_attack, bolt, strike, nuke 기본 4개
    # 기본 조회는 PK(sid) 기준
    assert 2001 in table   # auto_attack
    assert 2002 in table   # bolt
    assert 2003 in table   # strike
    assert 2004 in table   # nuke


def test_template_fields_parsed_correctly():
    """프로덕션 CSV 의 bolt 행이 파서를 통해 올바른 필드로 매핑되는지 체크.
    cooldown 의 '값' 자체는 데이터 튜닝으로 자주 바뀌므로 '숫자 파싱 성공'만 검증한다."""
    table = skill_data.load_from_csv()
    bolt = table.get(2002)
    assert bolt is not None
    assert bolt.name == "bolt"
    assert bolt.targeting == "Skillshot"
    assert isinstance(bolt.cooldown, float)
    assert bolt.cooldown >= 0.0
    assert bolt.projectile_speed > 0


def test_lookup_by_name_still_works():
    """리네이밍 위험 때문에 주 조회는 sid 지만 name 기반도 보조로 제공."""
    table = skill_data.load_from_csv()
    tpl = table.get_by_name("auto_attack")
    assert tpl is not None
    assert tpl.sid == 2001


def test_missing_lookup_returns_none():
    table = skill_data.load_from_csv()
    assert table.get(99999) is None
    assert table.get_by_name("nonexistent_skill") is None


def test_parses_custom_csv(tmp_path):
    """주석·빈줄 섞인 CSV 도 정상 파싱."""
    csv_file = tmp_path / "skills.csv"
    csv_file.write_text(textwrap.dedent("""\
        # 주석행 1
        # 주석행 2
        sid,name,targeting,projectile_speed,projectile_radius,projectile_range,projectile_lifetime,cooldown,cost,cast_range
        9001,test_skill,Homing,10.0,0.0,0.0,4.0,1.0,0,20.0
        9002,other,Skillshot,25.0,0.5,15.0,0.0,3.0,0,25.0
    """), encoding="utf-8")
    table = skill_data.load_from_csv(str(csv_file))
    assert len(table) == 2
    assert table.get(9001).name == "test_skill"
    assert table.get(9002).targeting == "Skillshot"


def test_missing_file_raises():
    with pytest.raises(FileNotFoundError):
        skill_data.load_from_csv("/nonexistent/path/to/skills.csv")


def test_duplicate_name_raises(tmp_path):
    csv_file = tmp_path / "dup.csv"
    csv_file.write_text(
        "sid,name,targeting,projectile_speed,projectile_radius,projectile_range,"
        "projectile_lifetime,cooldown,cost,cast_range\n"
        "1,dup,Homing,1,0,0,1,1,0,1\n"
        "2,dup,Skillshot,1,0,0,1,1,0,1\n",
        encoding="utf-8")
    with pytest.raises(ValueError, match="duplicate skill name"):
        skill_data.load_from_csv(str(csv_file))


def test_bindings_and_overrides_resolve_to_real_skills():
    """config.SKILL_BINDINGS 의 sid, SKILL_INPUT_MODE_OVERRIDES 의 sid 가
    실제 CSV 에 모두 존재하는지 — 설정 오타를 테스트로 차단."""
    import config
    table = skill_data.load_from_csv()
    for key, sid in config.SKILL_BINDINGS.items():
        assert isinstance(sid, int), f"binding '{key}' value should be int sid, got {type(sid)}"
        assert sid in table, f"binding '{key}' -> sid={sid} is missing from CSV"
    for sid in config.SKILL_INPUT_MODE_OVERRIDES:
        assert isinstance(sid, int), f"override key should be int sid, got {type(sid)}"
        assert sid in table, f"override sid={sid} is missing from CSV"
