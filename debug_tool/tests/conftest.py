"""
pytest 설정. tests/는 debug_tool/의 하위이므로 상위 디렉토리를 sys.path에 얹어
debug_tool 자체 모듈(client, network, config 등)을 임포트 가능하게 만든다.
"""

import os
import sys

_PKG_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
if _PKG_DIR not in sys.path:
    sys.path.insert(0, _PKG_DIR)
