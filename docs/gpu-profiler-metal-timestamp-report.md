# GPU 프로파일러 — Metal 타임스탬프 가용성 판정 (Task #22 항목 5)

> **레인:** L9 · **정본:** [gpu-profiler](./godot-gpu-profiler-implementation-research.md) §8 P3
> **베이스라인:** `6235d6e34b` · **브랜치:** `c3/l9-gpu-profiler`
> **형식:** [rd-capability-spike-report](./rd-capability-spike-report.md)와 동일 — 판정만이 아니라 **측정 수치와 그 수치가 틀렸을 조건**까지 적는다.

---

## 0. 판정 요약

macOS는 출하 타깃이다. 세 갈래로 나눠 답한다 — **RT 스텁 문제와는 별개 축**이므로 묶어서 결론내지 않는다.

| | 질문 | 판정 |
|:-:|---|---|
| **(a)** | Metal 백엔드가 타임스탬프를 **구현했는가** | 🔴 **아니오. 6개 엔트리포인트 전부 스텁이다** |
| **(b)** | 실기 디바이스가 **지원하는가** | 🟡 **한다 — 단 `AtStageBoundary` 하나뿐** |
| **(c)** | 호출자가 그 사실을 **알 수 있는가** | 🔴 **아니오. 이게 핵심 결함이다** |

**⇒ 현재 macOS에서 GPU 프로파일러는 모든 스코프에 대해 0ms를 보고한다. 오류도, 플래그도, 경고도 없다.**

---

## 1. (a) 구현 여부 — 전부 스텁 (`drivers/metal/rendering_device_driver_metal.cpp:2380-2402`)

```cpp
RDD::QueryPoolID RenderingDeviceDriverMetal::timestamp_query_pool_create(uint32_t p_query_count) {
    return QueryPoolID(1);                      // ← 유효해 보이는 ID를 반환
}
void RenderingDeviceDriverMetal::timestamp_query_pool_get_results(QueryPoolID, uint32_t p_query_count, uint64_t *r_results) {
    // Metal doesn't support timestamp queries, so we just clear the buffer.
    bzero(r_results, p_query_count * sizeof(uint64_t));   // ← 조용히 0으로 채움
}
void RenderingDeviceDriverMetal::command_timestamp_write(CommandBufferID, QueryPoolID, uint32_t) {
}                                                // ← 조용히 아무것도 안 함
```
`timestamp_query_pool_free` · `timestamp_query_result_to_time` · `command_timestamp_query_pool_reset`도 동일하게 빈 구현이다.

> **주석의 *"Metal doesn't support timestamp queries"* 는 (b)에서 보듯 사실이 아니다.** Metal은 지원한다 — 다만 Vulkan과 **모양이 다르다**(§4). 정확히 쓰면 *"이 드라이버가 아직 구현하지 않았다"* 이다.

---

## 2. (b) 디바이스 지원 — 실측 (Apple M2 Pro · macOS 26.5.2 · **이 머신에서 직접**)

`misc/metal_probes/metal_ts_probe.m`, `MTLDevice`에 직접 질의:

```
RESULT device=Apple M2 Pro
RESULT os=Version 26.5.2 (Build 25F84)
RESULT supportsCounterSampling[AtStageBoundary       ]=YES
RESULT supportsCounterSampling[AtDrawBoundary        ]=no
RESULT supportsCounterSampling[AtBlitBoundary        ]=no
RESULT supportsCounterSampling[AtDispatchBoundary    ]=no
RESULT supportsCounterSampling[AtTileDispatchBoundary]=no
RESULT counterSet=timestamp
RESULT hasTimestampCounterSet=YES
RESULT sampleTimestamps cpu=612817936968875 gpu=612817936968875 usable=YES
```

**읽는 법:**
- **`timestamp` 카운터 세트가 존재하고 `AtStageBoundary` 샘플링이 가능하다** → GPU 타이밍 자체는 얻을 수 있다.
- **나머지 4개 샘플링 지점은 전부 미지원.** 특히 `AtDrawBoundary`가 없다.
- **CPU↔GPU 타임스탬프가 동일 값으로 나온다**(`612817936968875`) → Apple Silicon은 두 타임라인이 **같은 타임베이스**다. **캘리브레이션(§P2)이 불필요**하다는 뜻이고, 이건 Vulkan 대비 유리한 점이다.

---

## 3. (c) 호출자 신호 — **없다. 이게 진짜 문제다**

- `RenderingDeviceCommons::Features` 열거에 **타임스탬프 관련 항목이 없다**(`rendering_device_commons.h:1023`). `SUPPORTS_MULTIVIEW`·`SUPPORTS_RAY_QUERY` 등 16개가 있지만 타임스탬프는 없다.
- `timestamp_query_pool_create`가 **유효해 보이는 `QueryPoolID(1)`** 을 돌려준다 → 생성 실패로도 안 보인다.
- `command_timestamp_write`가 **조용히 no-op**.
- `get_results`가 **0으로 채움**.

⇒ **호출자가 "지원 안 됨"과 "정말 0ms"를 구별할 방법이 전혀 없다.**

### 왜 이게 없는 것보다 나쁜가

프로파일러가 **"GPU 시간 0ms"** 를 그린다. 사용자는 그걸 *측정된 사실*로 읽고 **GPU가 병목이 아니라고 판단**한다. 없으면 다른 도구(Instruments/Xcode GPU capture)를 찾겠지만, 0ms는 **적극적으로 잘못된 방향을 가리킨다.** 최적화 판단을 오도하는 계측은 계측이 아니다.

---

## 4. ⚠️ 설계 제약 — `AtStageBoundary` 전용은 RD API 모양과 맞지 않는다

이게 P3(Metal 타이밍 구현)의 핵심 난점이고, **P1 설계에도 영향**을 준다.

| | Vulkan | Metal (Apple Silicon) |
|---|---|---|
| 타임스탬프 기록 | `vkCmdWriteTimestamp` — **커맨드 스트림 임의 지점** | 인코더에 **부착**(`MTLRenderPassDescriptor.sampleBufferAttachments`) |
| RD 추상 | `command_timestamp_write(cmd, pool, index)` — 임의 지점 | **표현 불가** |

RD의 `command_timestamp_write`는 **"지금 이 지점에 타임스탬프를 찍어라"** 라는 Vulkan 모양이다. Metal은 **패스(인코더) 경계에서만** 가능하므로, 임의 지점 호출을 그대로 받을 수 없다.

⇒ **Metal 구현은 "스텁 채우기"가 아니라 구조 변경이다.** `command_timestamp_write` 호출을 **다음 인코더 경계로 지연·병합**하거나, RD 추상에 **스코프 기반 API를 추가**해야 한다.

**항목 3(계층 깊이 상한)에도 직접 영향한다:** Metal에서 계층 깊이는 쿼리 풀 크기가 아니라 **패스 구조**가 상한이다. 두 백엔드의 상한 근거가 다르므로, 깊이 상한을 단일 숫자로 잡으려면 **더 제약적인 쪽(Metal)** 을 기준으로 삼아야 한다.

---

## 5. 제안 — 최소 변경으로 (c)부터 닫는다

(a)를 채우는 건 §4대로 구조 작업이고 P3 범위다. 그러나 **(c)는 지금 닫아야 한다** — 구현 전까지 계속 거짓 0ms가 나가기 때문이다.

**최소 변경 (⚠️ RD 공개 API 4파일 = C1 독점):**
```cpp
// rendering_device_commons.h — Features 열거에 1줄
SUPPORTS_TIMESTAMP_QUERY,

// rendering_device.cpp — has_feature()에 case 1개
case SUPPORTS_TIMESTAMP_QUERY:
    return driver->has_feature(SUPPORTS_TIMESTAMP_QUERY);
```
드라이버 측 응답(Vulkan `true` / Metal 현재 `false`)은 **제 영역**이라 충돌 없이 붙일 수 있다.

**그리고 소비자 측:**
- `RenderingServerDefault`가 타임스탬프 캡처 전에 이 플래그를 확인하고, 미지원이면 **프로파일 영역을 비우거나 명시적 미지원 상태로 표시**한다.
- `EditorVisualProfiler`가 GPU 열에 **0ms가 아니라 "미지원"** 을 표시한다.

**대안(비권장):** 에디터가 `get_current_rendering_driver_name() == "metal"` 로 분기하는 방법. **하지 말 것** — 능력 질의를 드라이버 이름으로 대체하는 것은 D3D12/향후 백엔드에서 같은 문제를 반복한다.

---

## 6. 이 판정이 틀렸을 조건

| 수치/판정 | 틀릴 수 있는 조건 |
|---|---|
| `AtStageBoundary`만 지원 | **M2 Pro / macOS 26.5.2 단일 측정.** M3/M4, Intel Mac, 향후 macOS에서 다를 수 있다. 특히 `AtDrawBoundary`가 열리면 §4의 제약이 완화된다. **다른 Apple 기기에서 `misc/metal_probes/metal_ts_probe.m`를 재실행할 것.** |
| CPU↔GPU 동일 타임베이스 | 같은 단일 측정. 다만 Apple Silicon 통합 설계상 세대가 바뀌어도 유지될 가능성이 높다 |
| "6개 엔트리포인트 전부 스텁" | 베이스라인 `6235d6e34b` 기준. 업스트림이 구현하면 바뀐다(포크는 디스커넥트 상태라 자동 반영 안 됨) |
| "Features에 타임스탬프 항목 없음" | 동일 베이스라인 기준. C1의 훅 작업으로 추가될 수 있음 |
| Metal이 "지원한다"는 결론 | **`supportsCounterSampling`가 YES인 것과 Godot이 실제로 값을 얻는 것은 다르다.** 실제 `MTLCounterSampleBuffer` 생성 + `resolveCounterRange` 왕복은 **미검증**이다. gpu-profiler 문서 §7.5-③도 이 코드 경로를 "반박(0-3)"으로 표시하고 있어 **Apple 문서 직접 확인이 선행**되어야 한다 |

---

## 7. 미검증 — 다음 단계

- **`MTLCounterSampleBuffer` 생성 → `resolveCounterRange` 실제 왕복.** 지금은 **능력 질의까지만** 했다. 값이 실제로 나오는지는 별도 확인이 필요하고, 정본 문서가 이 경로를 반박 상태로 두고 있다.
- **다른 Apple 기기 재측정** (M3/M4/Intel).
- **D3D12 백엔드의 동일 3갈래 판정** — 미조사다. "미지원"이 아니라 "확인하지 않음"이다.

---

*작성: 2026-08-25 · L9 / godot-contributer-3 · 측정: Apple M2 Pro / macOS 26.5.2 (이 머신)*
