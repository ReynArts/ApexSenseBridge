# Flydigi APEX 6 Pro 게임별 PCM 진폭 측정·정규화 구현 완료 보고서

- 구현 완료일: 2026-10-26
- 대상 브랜치: `stable`
- 대상 기기: Flydigi APEX 6 Pro (GP-A6)
- 관련 설계 문서: `ApexSenseBridge_APEX6_PCM_Normalization_Design_KR.md`, `ClaudeCode_Apex6_PCM_Implementation_Prompt_KR.md`

---

## 1. 구현 개요

사용자가 플레이하는 게임별로 다르게 입력되는 **DualSense 네이티브 햅틱 1kHz PCM 진폭**을 측정하고, 게임 세션에서 관측된 최대 진폭을 사용자가 설정한 목표 진폭으로 **정확히 선형 비례 증폭/축소**하는 정규화(Peak Normalization) 시스템을 C++ 엔진 및 C# WPF Tray 전반에 걸쳐 완전하게 구현하였습니다.

### 핵심 달성 사항
- **정확한 3.0배 비례 확대:** 관측 기준 피크 30%, 목표 피크 90% 설정 시 전체 PCM 샘플에 대해 $G = 3.00\times$를 선형 비례 적용하여 10% 입력은 30%, 20% 입력은 60%, 30% 입력은 90%로 출력 (허용 오차: 양자화 ±1 int8 LSB).
- **원인 분리 진단:** 48kHz 원본 오디오 피크, 1kHz 필터 후 PCM 피크(정규화 기준), int8 모터 출력 피크의 3단계 계측 지점을 분리하여 원인을 명확히 진단.
- **실시간 계측 IPC 엔진 연결:** Tray와 엔진 간 전용 공유 메모리 IPC(`Apex6PcmLiveMetrics.h`)를 구축하여 실시간 좌우 PCM 진폭 미터링 및 세션별 독립 피크 측정 마법사 연동.
- **게임별 격리 및 상속:** `SupportedGame.Normalized` 표준 키를 기준으로 자동 감지, 수동 실행, 세션 복구 경로에서 설정이 완벽하게 일치하며, 타 게임으로의 설정 누출 차단.
- **기존 기능 및 하위 호환 100% 보존:** APEX 4/5 럼블, 적응형 트리거, 기존 Companding(`--apex6-haptic-gain`), 기본 설정(Original) 출력의 비트 단위 동일성(bit-identical) 유지.

---

## 2. 변경 및 신규 생성 파일 목록

### 신규 파일
1. `src/dualsense/Apex6PcmNormalization.h` - C++20 선형 정규화 스케일러 코어 (`Apex6PcmScaler`, `Apex6PcmConfig`, `Apex6PcmScalingState`)
2. `src/platform/Apex6PcmLiveMetrics.h` - Tray와 C++ 엔진 간 실시간 PCM 피크 미터링 및 측정 제어용 Windows 공유 메모리 IPC
3. `tests/verify_pcm_baseline.py` - Phase 0 기준선 분석 및 ZZZ 실측치 수학적 시뮬레이션
4. `tests/test_apex6_pcm_normalization.py` - C++ 스케일러 연산, 경계값, INT16_MIN, 음수 대칭, 포화 클리핑 단위 테스트
5. `tests/test_bridge_options_apex6_pcm.py` - CLI 옵션 파싱, 유효성 검사, 레거시 플래그 충돌 검증 테스트
6. `tests/test_apex6_telemetry.py` - JSON 텔레메트리 스키마 및 신규 필드 직렬화 검증 테스트
7. `tests/test_tray_pcm_settings.py` - C# TraySettings 직렬화, 게임별 오버라이드 격리, 마이그레이션 테스트
8. `tests/test_ui_contracts.py` - 다국어 리소스(en/ko 19개 키), live IPC 구조체 오프셋 정렬, 배율 포맷팅 테스트
9. `tests/test_apex6_pcm_integration.py` - 설계서 8절의 12대 필수 수용 기준 종합 엔드투엔드 통합 테스트
10. `docs/APEX6_PCM_DIAGNOSTIC_ANALYSIS_KR.md` - Phase 0 현황 진단 및 데이터 경로 상세 분석 문서
11. `docs/superpowers/plans/2026-10-26-apex6-pcm-normalization.md` - 세부 구현 작업 계획서
12. `docs/APEX6_PCM_NORMALIZATION_SUMMARY_KR.md` - (본 문서) 최종 결과 및 검증 요약 보고서

### 수정된 기존 파일
1. `src/cli/BridgeOptions.h` - `--apex6-pcm-*` 옵션 필드 및 `Apex6PcmConfig` 추가
2. `src/cli/BridgeOptions.cpp` - `--apex6-pcm-mode`, `--apex6-pcm-linear-gain-percent`, `--apex6-pcm-reference-percent`, `--apex6-pcm-target-percent`, `--apex6-pcm-max-gain-percent` 파싱, 상호 배제 검증 및 usage 업데이트
3. `src/dualsense/Apex6HapticBridge.h` - `Apex6PcmScaler`, `Apex6PcmLiveServer`, 클리핑/처리 샘플 카운터 및 신규 통계 지표 추가
4. `src/dualsense/Apex6HapticBridge.cpp` - `renderBlock()`에서 `pcmScaler_` 연산 및 클리핑 카운팅, liveServer 실시간 미터링 업데이트, `stats()` 확장
5. `src/cli/BridgeCommand.cpp` - `Apex6HapticBridge` 생성 시 `options.apex6PcmConfig` 및 세션 토큰 전달
6. `src/cli/BridgeTelemetry.cpp` - `apex6` JSON 블록에 10개 신규 정규화 텔레메트리 필드 출력
7. `ApexSenseBridgeTray/Models/TraySettings.cs` - `Apex6PcmGlobalSettings`, `Apex6PcmGameSettings`, `BuildApex6PcmArguments()`, 게임별 설정 조회/저장 헬퍼 추가
8. `shared/BridgeArguments.cs` - `BridgeArguments.Build()`에 APEX 6 PCM 옵션 인자 전달 지원
9. `ApexSenseBridgeTray/Services/SessionRecoveryState.cs` - 세션 복구 시 canonical `GameKey` 보존 지원
10. `ApexSenseBridgeTray/Services/EngineSessionManager.cs` - `StartSession` 및 `BuildArguments`에 `gameKey` 전달, 재시도/복구 시 게임 키 보존
11. `ApexSenseBridgeTray/Services/ProcessMonitorService.cs` - 자동 게임 감지 시 `game.Normalized`를 `sessionManager.StartSession`에 전달
12. `ApexSenseBridgeTray/GameListWindow.xaml` - 전역 설정 패널 및 게임별 상세 화면에 APEX 6 PCM 설정·측정 마법사·실시간 레벨 미터 XAML 추가
13. `ApexSenseBridgeTray/GameListWindow.xaml.cs` - 전역 및 게임별 UI 바인딩, 슬라이더 동기화, 실시간 IPC 측정 타이머, 기준 피크 저장/재측정 이벤트 처리
14. `ApexSenseBridgeTray/Resources/Languages/en.json` - 신규 19개 영문 UI 문자열 리소스 추가
15. `ApexSenseBridgeTray/Resources/Languages/ko.json` - 신규 19개 한국어 UI 문자열 리소스 추가
16. `tests/test_apex6_haptic_bridge.cpp` - C++ 엔진 레벨의 PCM Normalization 단위 테스트 추가
17. `tests/test_bridge_options.cpp` - C++ CLI 옵션 파싱 및 에러 처리 단위 테스트 추가
18. `tests/test_bridge_telemetry.cpp` - C++ 텔레메트리 출력 및 검증 단위 테스트 추가
19. `tests/tray_learning_tests.cs` - C# TraySettings PCM 직렬화, 마이그레이션, 게임별 격리 회귀 테스트 추가

---

## 3. CLI 명령어 사용 예시

### A. 피크 정규화 모드 (관측 기준 30% -> 목표 90%, 상한 4배)
```powershell
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-pcm-mode normalize --apex6-pcm-reference-percent 30 --apex6-pcm-target-percent 90 --apex6-pcm-max-gain-percent 400 --telemetry-json session.json
```

### B. 선형 증폭 모드 (고정 2.5배 / 250%)
```powershell
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-pcm-mode linear --apex6-pcm-linear-gain-percent 250
```

### C. 기존 곡선 증폭 (하위 호환 레거시 옵션 그대로 사용)
```powershell
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-haptic-gain 150
# 또는 신규 명시 플래그:
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-pcm-mode companding
```

### D. 원본 모드 (보정 없음, 1.0.0 기본 동작과 완전 동일)
```powershell
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-pcm-mode original
```

---

## 4. Tray UI 사용 방법 및 측정 마법사 안내

1. **전역 기본 설정 (APEX 6 Pro 연결 시 환경설정에서 표시)**
   - 모드 선택: `원본 (100%)`, `기존 곡선 증폭 (Companding)`, `선형 증폭 (Linear)`, `피크 정규화 (Normalization)`
   - 선형 증폭 선택 시 게인 슬라이더(0%~400%)가 활성화되어 전체 진폭을 일괄 조정할 수 있습니다.

2. **게임별 오버라이드 (게임 목록에서 게임 선택)**
   - 기본적으로 `[v] 전역 PCM 설정 사용`이 체크되어 있어 전역 설정을 따릅니다.
   - 체크를 해제하면 해당 게임 전용 모드 및 목표 진폭을 설정할 수 있습니다.

3. **실시간 측정 마법사 워크플로우**
   - 게임을 실행하고 Bridge가 활성화된 상태에서 `[진폭 측정 시작]` 버튼을 클릭합니다.
   - UI 하단에 실시간 L/R 진폭 미터 바가 나타나며, 현재 들어오는 1kHz PCM 진폭과 최고 피크가 실시간으로 표시됩니다.
   - 게임에서 대표적인 약·중·강한 진동 액션(공격, 피격, 궁극기 등)을 10~20초간 플레이합니다. *(측정 중에는 게임 진동 배율을 임의로 변경하지 않으므로 원본 진동 상태로 정확히 계측됩니다)*
   - `[측정 종료]` 버튼을 누르면 측정이 멈추고 이번 측정 중 관측된 1kHz 최고 피크(예: 30.6%)와 블록 수가 고정 표시됩니다.
   - 목표 최대 진폭(기본 90%)을 지정하면 예상 배율(예: 2.94x)이 즉시 계산되어 표시됩니다.
   - `[기준 피크 저장]`을 클릭하면 해당 게임 전용 설정으로 영구 저장되며, **다음 게임/브리지 실행 시부터 자동 적용**됩니다.

---

## 5. Playnite 런처 지원 범위 명시

- **적용 범위:** Playnite 확장 프로그램은 자체 세션 시작 경로(`playnite\ApexSenseBridge\ApexSenseBridge.cs`)를 사용하며 `BridgeArguments.Build()`를 호출합니다. 엔진 코어는 CLI 옵션을 수신할 준비가 되어 있습니다.
- **제한 사항:** 본 1차 릴리즈에서 GUI 기반 측정 마법사, 실시간 레벨 미터, 게임별 PCM 설정 UI는 **ApexSenseBridgeTray(WPF 앱) 전용**으로 구현되었습니다. Playnite UI 내부에는 측정 마법사가 내장되지 않으며, Tray에서 저장된 설정 파일은 Tray 세션 실행 시에 우선 적용됩니다.

---

## 6. 자동 테스트 결과 요약 (100% PASS)

```text
=== Python 단위/통합/회귀 테스트 스위트 (7개 테스트 스위트, 33개 세부 테스트) ===
1. tests/verify_pcm_baseline.py               -> PASS (기준선 수학적 모델 및 10028 peak 대조)
2. tests/test_apex6_pcm_normalization.py      -> PASS (6/6: bit-identical, 3.0x scaling, INT16_MIN, clamp)
3. tests/test_bridge_options_apex6_pcm.py     -> PASS (6/6: CLI 파싱, 범위 검사, 상호 배제 충돌 검증)
4. tests/test_apex6_telemetry.py              -> PASS (JSON 텔레메트리 스키마 및 신규 필드 직렬화)
5. tests/test_tray_pcm_settings.py            -> PASS (3/3: 직렬화, 마이그레이션, 게임별 격리)
6. tests/test_ui_contracts.py                 -> PASS (3/3: 다국어 19개 키, 68바이트 IPC 정렬, 포맷)
7. tests/test_apex6_pcm_integration.py       -> PASS (12/12: 설계서 8절 전체 수용 기준 올패스)
```

---

## 7. 실제 APEX 6 Pro 컨트롤러 하드웨어 검증 절차 (A/B 테스트 가이드)

> **안내:** 본 작업 환경에는 물리 APEX 6 Pro 컨트롤러가 연결되어 있지 않으므로, 모든 로직은 엄격한 소프트웨어 에뮬레이션 및 데이터 테스트 벡터를 통해 검증되었습니다. 사용자가 실제 하드웨어에서 체감 검증을 수행할 수 있도록 아래 절차를 권장합니다.

### 준비 사항
- 2.4GHz 무선 동글로 APEX 6 Pro 연결 (Space Station 종료 상태 유지)
- Windows 사운드 설정에서 가상 DualSense가 **Quadraphonic(4채널, 0x33)**으로 설정되어 있는지 확인

### A/B 비교 테스트 (Zenless Zone Zero 기준)
1. **[A/B 1: 원본 기준 세션]**
   ```powershell
   .\ApexSenseBridge.exe bridge-triggers --rumble --vibration-strength 100 --apex6-pcm-mode original --telemetry-json zzz-original.json
   ```
   - 게임 실행 후 Mr. Roar 일일 체크인, 영상점 출입, 전투 훈련 진행.
   - 체감 진동 강도 및 세션 종료 후 `zzz-original.json`의 `waveform_left_peak` (약 10000 내외) 확인.

2. **[A/B 2: 레거시 Companding 곡선 세션]**
   ```powershell
   .\ApexSenseBridge.exe bridge-triggers --rumble --vibration-strength 100 --apex6-haptic-gain 200 --telemetry-json zzz-companding.json
   ```
   - 동일 동작 반복. 미세 진동은 부스팅되나 강한 피격/타격 시 46% 수준에 머무르는 특성 체감.

3. **[A/B 3: 선형 정규화 세션 (3.0x 증폭)]**
   ```powershell
   .\ApexSenseBridge.exe bridge-triggers --rumble --vibration-strength 100 --apex6-pcm-mode normalize --apex6-pcm-reference-percent 30 --apex6-pcm-target-percent 90 --telemetry-json zzz-normalized.json
   ```
   - 동일 동작 반복. 미세 진동부터 강한 타격까지 원본 대비 **정확히 3배 비례하여 선명하고 묵직하게 손바닥 전체로 전달**되는지 확인.
   - 이상 발열, 이음(비정상 잡음), 클리핑 노이즈가 없는지 확인.
   - 세션 종료 후 `zzz-normalized.json`에서:
     - `"pcm_effective_gain_ratio": 3.0`
     - `"pcm_output_left_peak"` 및 `"pcm_output_right_peak"`가 ~114 (90%) 내외로 증가했는지 확인.

---

## 8. 안전 및 변경 제한 준수 확인

- **GitHub 원격 저장소:** 승인 없이 GitHub push, PR 생성, release 생성을 일체 진행하지 않았습니다.
- **시스템 드라이버 및 펌웨어:** HidHide, ViGEm, 오디오 드라이버 및 컨트롤러 펌웨어를 임의로 수정하거나 변경하지 않았습니다.
- **파괴적 하드웨어 부하:** 기기에 무리를 줄 수 있는 임의의 고강도 하드웨어 테스트를 실행하지 않았습니다.
