# Sound assets

debug_tool 은 기본적으로 `feedback.py` 의 `AudioManager` 가 **사운드를 절차적으로
생성**해서 재생합니다. 즉 이 폴더가 비어 있어도 피격/스킬/스폰 사운드가 들립니다.

더 자연스러운 소리를 쓰고 싶으면 아래 이름으로 `.wav` 파일을 여기 두기만 하면
AudioManager 가 자동으로 합성본 대신 파일을 로드합니다.

| 이벤트 | 파일명 | 트리거 |
|---|---|---|
| `hit` | `hit.wav` | 다른 유닛이 피격당함 (기본 데미지) |
| `crit` | `crit.wav` | 30 데미지 이상 |
| `me_hit` | `me_hit.wav` | 로컬 플레이어가 피격 |
| `skill_cast` | `skill_cast.wav` | 투사체/스킬 발사 |
| `spawn` | `spawn.wav` | 몬스터 스폰 |

권장 포맷: 22050Hz, mono, 16-bit PCM `.wav` (짧게, 100~300ms). mixer 는
`config.ENABLE_AUDIO = False` 로 끌 수 있고, 전역 볼륨은 `config.AUDIO_VOLUME`
(0.0~1.0) 로 조절합니다.
