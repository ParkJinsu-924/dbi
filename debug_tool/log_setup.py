"""
로깅 설정 진입점.

main()에서 setup_logging()을 한 번 호출하면 전체 모듈이 공통 포맷/핸들러를 쓴다.
- 로거 이름은 "net" / "login" / "game" / "hit" 네 개를 쓰며,
  형식은 "[<name>] <LEVEL> <message>" 이므로 기존 "[net] ..." 접두사가 자연스럽게 이어진다.
- config.LOG_FILE이 비어 있지 않으면 같은 내용을 파일에도 기록한다.
"""

import logging
import sys

import config


def setup_logging() -> None:
    level_name = (config.LOG_LEVEL or "INFO").upper()
    level = getattr(logging, level_name, logging.INFO)

    fmt = logging.Formatter("[%(name)s] %(levelname)s %(message)s")

    root = logging.getLogger()
    # idempotent: 여러 번 호출돼도 핸들러가 중복 등록되지 않도록 초기화
    for h in list(root.handlers):
        root.removeHandler(h)
    root.setLevel(level)

    console = logging.StreamHandler(sys.stdout)
    console.setFormatter(fmt)
    root.addHandler(console)

    if config.LOG_FILE:
        file_h = logging.FileHandler(config.LOG_FILE, encoding="utf-8")
        file_h.setFormatter(fmt)
        root.addHandler(file_h)
