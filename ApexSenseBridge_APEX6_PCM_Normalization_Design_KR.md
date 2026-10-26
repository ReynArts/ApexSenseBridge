# ApexSenseBridge — APEX 6 Pro 게임별 PCM 진폭 측정·정규화 설계서

- 문서 버전: 1.0 / 2026-10-26
- 대상: `ReynArts/ApexSenseBridge` `stable` 브랜치를 기준으로 한 사용자 포크
- 실행 담당: Claude Code (C++ 엔진, C# WPF Tray, 테스트)
- 우선순위: **정확한 원인 진단 → 기능 구현 → 실제 A/B 검증**
- 상태: 구현 요구사항 및 설계 제안 (구현 또는 하드웨어 검증 완료를 의미하지 않음)

## 0. 의도 및 성공 조건

사용자가 원하는 것은 게임별로 다르게 들어오는 *DualSense 네이티브 햅틱 PCM*의 진폭을 측정하고, 그 게임에서 관측한 최대 진폭을 원하는 목표 진폭으로 **선형 비례 확대/축소**하는 기능이다.

- 예: 관측 기준 피크 `30%`, 목표 `90%` → 증폭 배율 `3.0x`.
- 같은 채널의 원본 `10%` → 출력 `30%`, 원본 `20%` → 출력 `60%`, 원본 `30%` → 출력 `90%`.
- 원본 샘플 간 상대적인 비율/부호/시간축/좌우 균형은 가능한 한 유지한다. **컴팬딩은 별도의 선택 모드이며, 정규화의 정의가 아니다.**
- 하드웨어 전송 PCM의 표현 가능 범위(`[-127,127]`로 다루는 현 엔진 경로)를 넘으면 포화 처리(saturate)하고, clipping 발생을 텔레메트리로 보고한다. 127이 모터의 물리적 최대 출력이라는 뜻은 아님.
- APEX 6 Pro 이외의 모델과 일반 진동/적응형 트리거의 기본 동작은 바꾸지 않는다.
- 기본 설정은 **기존과 비트 단위로 동일하게** 유지하고 기능은 opt-in이다.

## 1. 먼저 확인할 사실: '게임 문제'와 'APEX 6 변환 문제' 분리

APEX 4/5와 APEX 6은 동일한 햅틱 변환 경로를 사용하지 않는다. APEX 4/5는 `RumbleBridge`가 오디오의 envelope/energy를 모터 진동 명령으로 바꾸고, APEX 6은 가상 DualSense에서 받은 PCM 파형을 별도 경로로 전송한다. 따라서 APEX 5가 강하다고 APEX 6 수신 코드가 반드시 잘못된 것은 아니다. 게임 원본, 48kHz→1kHz 변환, 소프트웨어 스케일링, 전송 누락, 모터의 주파수 응답 중 어디에서 문제가 발생하는지 계측해야 한다.

### 실제 데이터 경로(현재 코드 기반)

```text
게임의 DualSense 햅틱 출력
  ├─ 가상 DualSense 오디오 장치 / 4채널(quadraphonic)
  │   ├─ 원본 48 kHz 햅틱 L/R 피크·RMS (가능할 때만 측정)
  │   └─ 필터링/디시메이션 → 1 kHz signed 16-bit PCM L/R
  ├─ VIIPER 피드백 프레임 해독 (8 samples/channel/block)
  ├─ Apex6HapticBridge::handle(): 입력 피크/시퀀스/드롭 계측 및 큐 적재
  ├─ Apex6HapticBridge::renderBlock():
  │     native PCM → PCM mode/gain → 일반 출력 강도(<=100%) → int8 L/R
  │     native PCM 없을 때만 envelope / ordinary rumble fallback
  └─ writeApex6Haptics(): APEX 6 전용 HID 모터 블록 출력
```

**중요:** `test-rumble`은 120 Hz 진단용 사인파로서 게임 네이티브 PCM 재생과 동일 조건이 아니다. 자체 진단이 강하다는 사실만으로 소프트웨어의 30% 손실을 확정하지 않는다.

### 조사해야 할 필수 원인 지표

| 지표 | 의미 | 우선 확인 포인트 |
|---|---|---|
| `audio_format`, `audio_mix_channels`, `audio_channel_mask` | Windows 가상 DualSense 오디오 구동 상태 | 정상 `quadraphonic`, 4채널, 0x33(51) |
| `raw_haptic_left/right_peak` | 48kHz 원본 PCM 피크(지원하는 백엔드에 한함) | 이 단계부터 낮은지 |
| `waveform_left/right_peak` | 1kHz 필터링 후 signed16 PCM 피크 | 원본 대비 과도하게 줄어드는지 |
| `pcm_output_left/right_peak` | 모터 전송 전의 int8 PCM 출력 피크 | gain 미적용/양자화 손실 여부 |
| `waveform_active_drops`, `waveform_underruns`, `waveform_stale_drops`, `waveform_sequence_gaps` | 큐/스트림 품질 | 순간 햅틱 소실 여부 |
| `audio_haptics_received/delivered/coalesced` | 가상 장치 수신/전달 계수 | 재생 경로에서 유실이 발생했는지 |
| `write_failures`, `deadline_overruns` | 물리 장치 출력 문제 | HID/타이밍 이상 여부 |

- 48kHz raw와 1kHz filtered 피크는 **동일한 주파수 대역을 측정하지 않는다**. 값 차이만으로 버그라고 판단하지 말고 주파수 스펙트럼/필터 설계도 확인한다.
- 피크 비율은 **PCM 진폭**의 비율이지 모터의 물리적 진동 가속도나 인간이 느끼는 세기의 비율이 아니다.
- `raw_*` 값이 없거나 누락된 경우 0%로 표시하지 말고 `측정 불가`로 구분한다.
- `waveform_*_peak`는 현재 *수신된 블록*을 계측하므로, 이후 버려진 블록의 피크도 포함할 수 있다. `rendered_peak`/`drop count`를 함께 표시하거나 진단 설명에서 이를 명시한다.

## 2. 제공할 사용자 기능

### A. 전역 기본 설정

- 기능 기본값: `Original / 보정하지 않음` (현재 동작과 완전 동일)
- 처리 모드: `Original`, `Companding (legacy)`, `Linear gain`, `Peak Normalization`
- `Linear gain`: 100% = 1배, 최대 400% = 4배(실험적 상한, 하드웨어 안전 보장이 아님). 감쇠가 필요하면 0~99%도 내부 엔진은 지원할 수 있음. UI 기본 범위는 제품에서 합리적으로 조정.
- `Companding`: 기존 `--apex6-haptic-gain 100..200` 동작 그대로 유지.
- `Peak Normalization`: 측정된 기준 피크와 목표 피크로 고정 배율을 산출. 기본 목표 90%는 **사용자 선택 시에만** 적용, 기본은 보정 꺼짐.
- 수동 값/슬라이더 동기화, 초기화 버튼, 예상 유효 배율 표시.
- PCM만 변경. APEX 4/5 진동 설정·APEX 6 일반 rumble 및 trigger에는 영향을 주지 않는다.

### B. 게임별 오버라이드

- 각 게임별 `[전역 설정 사용]` 기본 ON, OFF일 때 모드/목표 피크/수동 배율/측정된 기준 피크를 따로 저장.
- 게임 키는 표시 제목이나 프로세스 PID가 아니라 **기존 `SupportedGame.Normalized`와 같은 안정적인 정규화 ID**를 우선 사용. 미등록·수동 모드에 대해서는 명시적인 별도 정책 사용(제멋대로 title을 키로 쓰지 않음).
- 자동 게임 감지, 게임 라이브러리에서 수동 실행, 세션 중단 후 재시작/복구 모두 같은 게임 키와 프로필을 전달해야 함.
- 게임별 설정이 없으면 전역 기본값, 아예 게임을 식별하지 못하면 안전한 전역/Original 기본값 적용. 타 게임 설정 누출 금지.
- Playnite 런처 경로는 처음부터 지원 가능한지 조사하고, 불가능하면 **범위를 명시하고 독립된 테스트**를 둔다. Tray에서만 적용되는데 Playnite도 적용되는 척하면 안 됨.

### C. 측정(캘리브레이션) 마법사

1. 사용자가 게임 선택 후 `진폭 측정 시작` → UI는 읽기 전용 계측 상태로 들어감. **이 단계에서 증폭하지 말 것.**
2. 원본 PCM 처리 경로, 필터 후 PCM, motor 출력 PCM, active drops, underruns를 기록. 게임의 대표적인 약·중·강한 진동 장면을 플레이.
3. `측정 종료` → 기준 피크(좌/우/통합), 측정 지속 시간, 유효 PCM 활성 블록 수, 클리핑/유실 여부, 오디오 채널 설정을 표시.
4. `기준 피크 저장`(확인 버튼), `다시 측정`, `수동 입력` 제공.
5. 사용자가 `목표 피크(예: 90%)`를 선택하면 예상 gain 계산 및 경고. 다음 실행부터 해당 게임에 적용. 첫 버전에서는 **재실행 필요**를 UI에 명시.
6. 첫 버전은 실제로 지원되는 `--telemetry-json` 세션 종료 파일을 활용해도 됨. 실시간 차트를 만들려고 임의의 IPC 프로토콜을 추가하지 않는다. 측정 중인 현재 값을 보여주려면 별도 지원되는 상태 인터페이스를 정의해야 함.

**측정의 한계:** 화면에 `게임 최대 진동`이라고 단정하지 말고 `이번 측정에서 관측한 PCM 최고 진폭`으로 표기한다. 게임 전체에서 이후 더 큰 이벤트가 나올 수 있다.

## 3. 연산 규격(핵심)

- 입력은 `int16 PCM [-32768,32767]`, 출력은 전용 경로의 `int8 [-127,127]`로 가정하며 기존 코드의 실제 부호/양자화 규칙과 대조해야 한다.
- 기준 피크는 **1kHz로 변환된, 보정 전의 PCM**에서 취한다. 원본 48kHz 피크는 원인 분석용으로 별도 보존한다. 서로 다른 단계의 피크를 섞어 gain 계산하지 않는다.
- 입력 피크 `P_ref = max(max_abs(left), max_abs(right))` (양 채널 공통 배율을 사용하여 스테레오 비율 보존).
- 목표 피크 `P_target = round(full_scale * targetPct / 100)`.
- 배율 `G = P_target / P_ref`. 퍼센트로는 `100 * targetPct / referencePct`.
- 하드웨어 검증 전에는 배율 상한을 configurable `G_max`로 두고 실험 초기값 상한을 `4.0x`로 제한한다. 이것은 안전성 검증 수치가 아니며, 과열·이상음·지속 고출력 검사는 별도 필요.
- `G_effective = clamp(G, 0, G_max)`. cap에 걸리면 UI에 `목표치 도달 불가(현재 배율 제한)`을 정확히 표시.
- 각 샘플 `y = saturate_int8_127(quantize(x * G_effective))`.
- 위 수식의 양자화는 16비트 정수 overflow, 특히 `abs(-32768)`에 주의. 64비트 정수/정확한 정수 연산 또는 검증된 float 변환을 사용하고, **100% Original 경로는 기존 코드 bit-identical**로 유지.
- 0 입력은 0, 부호는 유지, 좌·우 채널 cross-talk 금지, 시간축/원본 주파수는 변경하지 않는다.
- 예: 기준 30%, 목표 90%: 5→15 / 10→30 / 20→60 / 30→90(%). 
- 측정 피크가 0이거나 측정 데이터가 불충분하면 **정규화 금지**(Original로 fallback, UI 경고). `divide by zero` 금지.
- 나중에 더 강한 이벤트가 발생할 수 있으므로 출력 clip count / total samples / clipping rate 기록, threshold 이상 시 UI 경고 제공.
- 감쇠가 필요한 `reference=100%, target=90%`도 0.9x로 동작해야 함. gain=100% 이상만 허용하는 잘못된 공식을 피할 것.
- 유저가 정한 **고정 배율을 세션 시작 시 적용**, 게임 중 상대 피크를 따라 매 순간 바꾸는 AGC/자동 게인 펌핑 금지.

### 기존 Companding과 관계

현재 `src/dualsense/Apex6PcmGain.h`의 `scaleApex6Pcm()`은 `100..200` bounded companding 기능이다. 새 `Normalization`과 별개로 유지한다.

**호환성 계약:**
- 아무 새 옵션이 없으면 변경 전 출력과 bit-identical.
- 기존 `--apex6-haptic-gain 100..200`의 결과는 변경 전과 bit-identical.
- 기존 옵션과 새 전용 PCM 모드 옵션이 동시에 지정된 경우 조용히 우선순위로 덮어쓰지 말고 **명시적으로 오류** 처리하거나 README에 명확한 우선순위를 정해 테스트.
- `EffectStrength.h::scaleEffectStrength()`는 트리거에도 적용되는 공통 함수다. APEX 6 증폭을 위해 이 함수를 변경하지 말 것.
- 이미 100%가 넘는 `--vibration-strength`를 APEX 6에 무작정 허용하는 변경도 금지.

### 제안하는 CLI 인터페이스 (최종 명칭은 기존 파서 관례와 충돌 검사 후 결정)

```text
--apex6-pcm-mode original|companding|linear|normalize
--apex6-pcm-linear-gain-percent 0..400
--apex6-pcm-reference-percent 1..100
--apex6-pcm-target-percent 1..100
--apex6-pcm-max-gain-percent 100..400
--telemetry-json <file>                 # 기존 기능 활용
```

- `original`은 매개변수를 무시하는 대신 혼용 오류를 명시하는 쪽을 권장.
- `normalize`가 기준 피크를 갖고 있지 않으면 자동으로 임의의 수치를 추정하지 말고 원본 출력+상태 메시지.
- CLI 정수 퍼센트만 쓸 경우 미세한 계측 정밀도가 떨어질 수 있다. 내부 구조는 `referencePeakAbs16` 등 정확한 정수 피크를 저장하고 UI %는 표시용 계산을 권장한다.
- `--apex6-haptic-gain` 기존 파서는 유지(legacy alias/기존 동작). Native PCM 경로가 아닌 경우 새 옵션은 동작하지 않음을 설명.

## 4. 텔레메트리 확장

기존 `BridgeTelemetry.cpp`의 `apex6` JSON에 **하위 호환되는 필드**를 추가한다. 스키마가 있다면 이전 필드 의미 변경 없이 버전 또는 optional fields 확장.

```json
{
  "apex6": {
    "pcm_mode": "normalize",
    "pcm_reference_peak_abs16": 9830,
    "pcm_target_percent": 90,
    "pcm_requested_gain_ratio": 2.999,
    "pcm_effective_gain_ratio": 2.999,
    "pcm_gain_limited": false,
    "pcm_output_clipped_samples": 0,
    "pcm_processed_samples": 60000,
    "pcm_measurement_status": "valid",
    "pcm_observed_left_peak_abs16": 9200,
    "pcm_observed_right_peak_abs16": 9830
  }
}
```

위 JSON은 **출력 스키마 제안/형태 예시**일 뿐 실제 로그 값이 아니다. 기존 `raw_haptic_*`, `waveform_*`, `pcm_output_*`, `waveform_active_drops` 값은 보존.

- `clippedSamples / processedSamples`와 실제 피크 값을 기록하고 수신 데이터 0건이면 `unavailable`.
- 측정 run metadata: game key, UTC 시작/끝 시각, 유효 활성 블록 수, 통합 피크, 채널 형식 검증 유무, 버전/모델을 포함.
- 다음에는 이상치에 덜 민감한 `상위 1% 활성 블록 피크` 등 robust reference 후보를 제공할 수 있지만, **첫 버전은 관측 최고 피크를 정확히 보여주고 사용자가 수동 확인**하는 편이 명료함.

## 5. 설정 모델 및 런처 전달

### 저장 모델 제안

```json
{
  "Apex6PcmGlobal": {
    "Mode": "original",
    "LinearGainPercent": 100,
    "TargetPeakPercent": 90,
    "MaxGainPercent": 400
  },
  "Apex6PcmGames": {
    "zenless-zone-zero": {
      "UseGlobal": false,
      "Mode": "normalize",
      "MeasuredReferencePeakAbs16": 9830,
      "TargetPeakPercent": 90,
      "MaxGainPercent": 400,
      "MeasuredUtc": "2026-10-26T00:00:00Z",
      "MeasurementVersion": 1
    }
  }
}
```

`Apex6PcmGames`는 직렬화 예시이며 **기존 파일과 정확히 합치는 작업**이 필요. 기존 `tray_settings.json`, `ControllerCalibrations`, `ApexProfileSlots` 등은 훼손하지 않는다. C# `JavaScriptSerializer`의 null/missing fields 역직렬화, 필드 Normalize, 대소문자 무시 비교 및 이전 설정 마이그레이션을 시험한다.

**세션 시작 경로:**
- `ProcessMonitorService`가 인식한 `SupportedGame.Normalized`를 `EngineSessionManager`에 전달할 수 있게 한다.
- `EngineSessionManager::BuildArguments(...)`는 현재 `BridgeArguments.Build(...) + settings.BuildControllerCalibrationArguments()`로 구성됨. 새로운 PCM 옵션을 **검증된 APEX6 프로필 해석 결과**와 연결해야 함.
- 현재 관리자는 게임 제목을 갖지만 canonical normalized key를 받지 않는 경로가 있으므로 `gameKey`를 별도 전달·보존해야 함(리트라이/복구 포함). 제목 문자열만으로 강제 매칭하지 않음.
- APEX 6이 아닌 컨트롤러에서는 새 PCM 옵션을 무시하거나 모델별로 안전하게 배제하고, 기존 명령줄 검증 정책과 충돌하지 않게 처리.
- Playnite 설정/명령줄은 Tray와 별도. 최소 구현 범위를 명시하고 자동으로 공유된다고 가정하지 말 것.
- 터미널 인자 문자열 조합 시 사용자 제어 문자열은 escape/sanitize 하고, 모델/수치/모드는 타입 검증. 인자 주입 금지.

## 6. UI 상세

### 전역 설정(Apex 6 Pro 연결 시만)

```text
[APEX 6 Pro 네이티브 햅틱]
모드             (●) 원본  ( ) 기존 곡선 증폭  ( ) 선형 증폭  ( ) 피크 정규화
선형 증폭         [====|-----------] [ 150 ] %       # linear에서만 활성화
원본 기준 피크    [ 30.6 ] % [측정하기]             # normalize에서만
목표 피크         [==========|------] [ 90 ] %
계산된 배율        2.94 x (최대 허용 4.00 x)
[원본으로 초기화] [저장]
* 원본 피크는 이번 세션의 관측값이며 물리 모터 최대 출력이 아닙니다.
```

### 게임별 설정(각 게임 상세)

```text
[게임: Zenless Zone Zero]
[x] 전역 설정 사용                      # off로 하면 하단 수정 가능
모드: [피크 정규화 ▼]
측정된 PCM 피크: 좌 28.1% / 우 30.6% / 기준 30.6%
목표 최대 진폭: [==========|------] [90] %
예상 배율: x2.94 / 최대 출력: 90% (관측 기준)
[측정 시작] [측정 결과 저장] [게임별 초기화]
상태: 오디오 4채널 정상 / 유효 샘플 있음 / 클리핑 0 / 드롭 0
* 설정은 다음 게임 실행부터 적용됩니다.
```

- 측정 실패(실제 PCM 없음, 쿼드라포닉 미설정, 게임 미실행 등)를 `0%`와 구분해 사유를 표시.
- WPF 슬라이더/숫자 입력은 범위와 검증 공유, 입력 중 빈 문자열 허용하되 커밋 시 검증. 포커스/컨트롤러 조작/다국어 리소스 기존 패턴 유지.
- 고정 상한 4배는 '4배 안전'이란 의미가 아니므로 문구 주의. 이상 소음·발열 시 즉시 중단 안내.

## 7. 단계별 구현 계획 (반드시 각 단계마다 테스트)

### Phase 0 — 기준선·현상 진단 (우선)

- [ ] 포크 작업 브랜치 생성. 기준 SHA/태그/엔진과 Tray 빌드 성공 기록.
- [ ] 기존 `APEX6_DUALSENSE_VALIDATION.md`, `Apex6PcmGain.h`, 두 백엔드(`WindowsVirtualDualSense.cpp`, `WindowsLibViiperVirtualDualSense.cpp`) 동작 비교.
- [ ] `--telemetry-json`가 현재 release 빌드에서 어떻게 쓰이는지 점검.
- [ ] ZZZ 등 낮은 사례와 강한 사례의 raw peak / filtered peak / final peak / dropout 값을 기록하고 해석 문서 작성.
- [ ] APEX 6 하드웨어가 없으면 소프트웨어 테스트는 진행하되 **실측/체감 검증 미완료** 표시.

### Phase 1 — 엔진 연산과 테스트 (MVP 핵심)

- [ ] `Original`/`LegacyCompanding`/`Linear`/`Normalize` 모드를 APEX 6용 타입과 함수로 분리.
- [ ] 입력 16-bit → 출력 8-bit scaling, ±32768 음수 경계, clamp/clip counting 구현.
- [ ] 좌우 공통 배율, silent handling, 최대 배율, invalid measurement 처리.
- [ ] CLI 옵션 파싱/validation/legacy option 충돌 검사.
- [ ] 전체 적용 시 기존 CLI/엔진 기본값 회귀 없음.

### Phase 2 — 측정 및 진단

- [ ] 기존 peaks/raw/RMS/drop 테이블 활용, 측정 레코드 저장/추출.
- [ ] `피크 없음`, `가상 오디오 설정 오류`, `데이터 부족`, `과도한 드롭` 상태 분리.
- [ ] 텔레메트리에 requested/effective gain, clip count, processed samples 및 reference source 기록.
- [ ] 동일 장면 100% 원본 vs 정규화 A/B 검사 방법을 문서화.

### Phase 3 — Tray 게임별 설정 + UI

- [ ] `TraySettings.cs`에 전역 + 게임별 override + 측정값 모델.
- [ ] canonical game key를 실제 세션 시작/재시작/복구 경로에 전달.
- [ ] WPF slider + numeric field + 모드 탭/토글 + 측정 안내 + 저장.
- [ ] 옵션을 게임별 엔진 인자로 연결하되, 실행 중 변경은 다음 세션부터 적용.
- [ ] 기존 APEX4/5 UI, 필수 드라이버/HidHide, 사용자 설정 마이그레이션 회귀 테스트.
- [ ] Playnite 미지원 시 범위 명시, 지원 시 각각 별도 검증.

### Phase 4 — 실기 테스트와 품질 확인

- [ ] APEX6 Pro + 2.4GHz 동글에서 별도 짧은 진동 장면 반복.
- [ ] 원본/컴팬딩/선형/정규화 네 가지 A/B, 체감 강도뿐 아니라 디테일/좌우 분리/노이즈/지속 진동 확인.
- [ ] 원본 피크 30% 장면에서 정규화 90%가 로직상 3x인지, 실제 output peak가 예상 범위인지 확인.
- [ ] 이미 원본 PCM이 90~100%인 게임에서는 보정 켜도 무조건 더 좋아지는 것처럼 말하지 말 것. 불필요한 clip이 있으면 기본 OFF 유지.
- [ ] 이상 진동·발열·소음 발생 시 실기 테스트 즉시 종료, 사용자의 하드웨어 안전 우선.

## 8. 반드시 포함할 자동 테스트

1. **수학:** reference=30%, target=90% → 3.0x. 10→30%, 20→60%, 30→90% (양자화 오차 ±1 int8 LSB 허용).
2. **모드:** `Original`과 기존 `--apex6-haptic-gain` 각각 기존 테스트 벡터 bit-for-bit 동일.
3. **음수:** negative sample, INT16_MIN, 0, ±최대치; 오버플로우/랩어라운드 없을 것.
4. **클리핑:** 기준보다 큰 장면이 나중에 나오면 saturate, clip count 증가; 과대 샘플로 좌우 채널 변조 금지.
5. **채널:** L=10%, R=5%, common gain=3x → L=30%, R=15%; 상대 균형 유지.
6. **데이터 없음:** reference=0/missing, 오디오 silent, 쿼드라포닉 실패 → 원본 fallback + 이유 표시.
7. **설정:** 이전 `tray_settings.json` 역직렬화/신규 저장/재기동; per-game override vs global fallback vs unknown game.
8. **런처:** 자동 감지·수동 시작·중단 복구 시 동일 gameKey; 게임 A의 증폭이 B로 누출되지 않을 것.
9. **입력 검증:** 문자열/공백/음수/overflow/NaN(숫자 UI) 처리; 한글 등 게임명 안전성.
10. **회귀:** APEX4/5 진동·APEX6 일반 rumble 및 트리거·Steam/HidHide·측정 OFF 기본 동작 동일.
11. **통합:** `--telemetry-json` 출력 유효 JSON, 이전 필드 유지, clip 카운트와 peak 수치 일치.
12. **퍼포먼스:** 1kHz 출력에서 O(1) 연산/샘플, 렌더 루프에서 파일 쓰기·할당·UI 갱신 금지.

## 9. 필수 수용 기준(Definition of Done)

- [ ] 기본 설정 OFF 상태에서 기존 1.0.0 stable 출력 회귀 없음(기존 테스트 패스).
- [ ] 측정하면 필터 전/후 PCM 피크·최종 출력 및 drop 수치를 혼동 없이 확인 가능.
- [ ] 같은 1kHz PCM 기준 피크 30%를 90%로 설정하면 실제 배율 3x, 10% 샘플은 30% 출력(양자화 오차 허용).
- [ ] 기준값 0/미측정 시 배율 폭주 금지, fallback.
- [ ] 목표 달성에 4배 초과가 필요한 경우 제한 적용과 경고를 명확히 표시.
- [ ] 게임별 저장/전역 상속/앱 재시작/자동 게임 감지 정상.
- [ ] APEX4/5, 일반 rumble, 적응형 트리거의 의미/설정/경로에 영향 없음.
- [ ] Native C++와 C# WPF Tray 모두 빌드 성공 + 테스트 통과. 실패는 보고하고 숨기지 않음.
- [ ] 유저 확인 없이 원본 저장소에 push, release 생성, 드라이버 설치·교체, 펌웨어 변경, 패드에 고강도 하드웨어 테스트를 실행하지 않음.
- [ ] 사용자에게 수정 파일 목록, 설계와 실제 구현 차이, 테스트 결과, 미검증 하드웨어 사항을 명시.

## 10. 빌드/테스트 명령어

```powershell
.\scripts\build-windows.ps1
ctest --test-dir .\build-win -C Release --output-on-failure
.\scripts\build-tray-app.ps1
# 필요하면 저장소의 scripts/test-tray-controller-settings.ps1 등 해당 회귀 스크립트 추가 실행
```

실기용 A/B 명령은 먼저 기존 코드 확인 후 실행한다. 현재 구현에는 다음 baseline 예시가 존재한다:

```powershell
.\ApexSenseBridge.exe bridge-triggers --rumble --vibration-strength 100 --apex6-haptic-gain 100 --telemetry-json baseline.json
```

주의: Tray의 자동 활성화/기존 세션을 종료하고, 게임보다 브리지를 먼저 실행해야 한다. `--telemetry-json`은 정상 세션 종료 후 생성되는지 코드로 확인. 새 CLI 매개변수 명칭은 Phase 1 완료 뒤 `--help`와 문서를 일치시킨다.

## 11. 소스 탐색 링크

- 핵심 PCM 및 queue: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/dualsense/Apex6HapticBridge.cpp
- Companding 기존 코드: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/dualsense/Apex6PcmGain.h
- 16-bit waveform / raw 계측 구조: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/dualsense/DualSenseFeedback.h
- APEX 4/5 모터 경로: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/dualsense/RumbleBridge.cpp
- CLI 파서: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/cli/BridgeOptions.cpp
- CLI 실행/브리지 생성: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/cli/BridgeCommand.cpp
- JSON 지표 출력: https://github.com/ReynArts/ApexSenseBridge/blob/stable/src/cli/BridgeTelemetry.cpp
- Tray 설정: https://github.com/ReynArts/ApexSenseBridge/blob/stable/ApexSenseBridgeTray/Models/TraySettings.cs
- Tray 게임 목록/설정: https://github.com/ReynArts/ApexSenseBridge/blob/stable/ApexSenseBridgeTray/GameListWindow.xaml.cs
- Tray 게임 감지: https://github.com/ReynArts/ApexSenseBridge/blob/stable/ApexSenseBridgeTray/Services/ProcessMonitorService.cs
- Tray 세션/인자: https://github.com/ReynArts/ApexSenseBridge/blob/stable/ApexSenseBridgeTray/Services/EngineSessionManager.cs
- 공통 인자 빌더: https://github.com/ReynArts/ApexSenseBridge/blob/stable/shared/BridgeArguments.cs
- APEX 6 검증 문서: https://github.com/ReynArts/ApexSenseBridge/blob/stable/APEX6_DUALSENSE_VALIDATION.md
- 테스트: https://github.com/ReynArts/ApexSenseBridge/blob/stable/tests/test_apex6_haptic_bridge.cpp

---

### 범위·확실성 주석

위 문서는 공개 `stable` 소스에 근거한 기능 설계다. **실제 APEX 6 하드웨어에서 테스트하거나 구현했다는 뜻이 아니다.** 게임의 PCM 최고 진폭이 낮다는 사실만으로 현재 코드가 그만큼 '감쇠'시켰다고 단정하지 않는다. 배율 제한 및 출력 [-127,127] 유지가 모터의 열/기계적 안전성을 검증해 주지는 않는다.
