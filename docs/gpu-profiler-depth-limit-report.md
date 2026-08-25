# GPU 프로파일러 — 계층 깊이 상한 근거 (Task #22 항목 3)

> **레인:** L9 · **정본:** [gpu-profiler](./godot-gpu-profiler-implementation-research.md) §8 P0
> **베이스라인:** `6235d6e34b` · **브랜치:** `c3/l9-gpu-profiler`
> **전제:** [Metal 타임스탬프 판정](./gpu-profiler-metal-timestamp-report.md)에서 확인한 Metal 제약을 기준으로 잡는다(더 제약적인 쪽).

---

## 0. 결론 — **깊이는 상한을 걸 대상이 아니다**

항목 3은 "쿼리 풀 크기·프레임 지연을 재서 계층 깊이 상한을 근거 있는 숫자로 잡으라"였다. 재본 결과 **전제가 어긋난다**:

| | 실제로 무엇이 제약하는가 | 이미 어떻게 처리되는가 |
|---|---|---|
| **총 타임스탬프 수** | 프레임당 `max_timestamp_query_elements` (기본 **256**) | ✅ **이미 명시적 실패.** 조용한 절단 아님 |
| **깊이(중첩 단계)** | **아무것도 제약하지 않는다** | — |
| **타임스탬프 위치** | 패스 경계에만 가능 | ✅ 이미 3중 가드로 강제 |

⇒ **풀이 제약하는 것은 깊이가 아니라 총 개수이고, 그건 이미 큰 소리로 실패한다.** 깊이에 숫자를 매기는 건 존재하지 않는 제약을 발명하는 일이 된다. 아래는 근거다.

---

## 1. 총 개수 제약 — 이미 명시적 실패다

`rendering_device.cpp:8768`:
```cpp
ERR_FAIL_COND_MSG(frames[frame].timestamp_count >= max_timestamp_query_elements,
    vformat("Tried capturing more timestamps than the configured maximum (%d). "
            "You can increase this limit in the project settings under "
            "'Debug/Settings' called 'Max Timestamp Query Elements'.", max_timestamp_query_elements));
```
- 설정: `debug/settings/profiler/max_timestamp_query_elements` — **기본 256, 범위 256~65535** (`project_settings.cpp:1867`)
- 프레임 슬롯마다 풀을 하나씩 할당(`rendering_device.cpp:8537`)

**이미 요구사항을 만족한다** — 조용한 절단이 아니고, 실패 메시지가 **정확히 어떤 설정을 올리면 되는지** 알려준다. **새 정책이 필요 없다.** 오히려 제가 설계할 뻔한 것의 좋은 선례다.

---

## 2. 위치 제약 — 패스 경계 전용 (이미 강제됨)

`capture_timestamp`(`rendering_device.cpp`)는 **활성 리스트 안에서 그리기/디스패치가 시작된 뒤에는 거부**한다 — draw·compute·raytracing 3종 전부:
```cpp
ERR_FAIL_COND_MSG(draw_list.active && draw_list.state.draw_count > 0, "Capturing timestamps during draw list creation is not allowed. ...");
```

### ⚠️ 이전 보고 정정
[Metal 리포트](./gpu-profiler-metal-timestamp-report.md) §4에서 나는 *"RD의 `command_timestamp_write`가 Vulkan 모양(임의 지점)이라 Metal과 맞지 않는다"* 고 적었다. **드라이버 레벨에서는 맞지만, 공개 RD API 레벨에서는 틀렸다.**

`capture_timestamp`는 **이미 패스 경계로 제한**되어 있다 — 즉 Godot의 실제 사용 패턴은 Metal의 `AtStageBoundary`와 **이미 정합한다.** P3 Metal 구현은 내가 시사한 것보다 **덜 구조적**이다. 남는 불일치는 드라이버 인터페이스(`command_timestamp_write`)의 모양뿐이고, 그건 RD 내부에서 인코더 경계로 모으면 흡수된다.

---

## 3. 실측 — 실제 프레임이 쓰는 양

동일 씬(3D, 그림자 켠 방향광 1 · 박스 24 · SSAO · Glow · 절차적 하늘), 동일 플래그 `--gpu-profile`:

| 백엔드 | 결과 |
|---|---|
| **Vulkan** (NVIDIA GB10 / Linux) | 이름 붙은 태스크 **8개**, **총 0.383 ms** |
| **Metal** (Apple M2 Pro / macOS) | `GPU PROFILE (total 0.0ms):` — **항목 0개** |

Vulkan 측 내역:
```
-Render Depth Pre-Pass                 0.0127 ms
-Setup Shadows                         0.0267 ms
-Render Directional/SpotLight Shadows  0.0112 ms
-Process SSAO                          0.0581 ms
-Render Opaque Pass                    0.1115 ms
-Render Sky                            0.0113 ms
-Tonemap                               0.0394 ms
-Glow                                  0.0684 ms
```

**두 가지를 동시에 보여준다:**
1. **여유는 크다.** 이 정도 씬이 태스크 8개다(원시 타임스탬프는 `>`/`<` 마커 포함이라 더 많지만 256에는 한참 못 미친다). **기본 256이 좁아서 깊이를 제한해야 하는 상황이 아니다.**
2. **§2 Metal 판정의 사용자 가시 형태.** 같은 코드·같은 플래그·같은 씬인데 한쪽은 쓸 수 있는 프로파일, 다른 쪽은 **빈 0.0ms에 아무 설명 없음.**

---

## 4. 깊이에 굳이 상한이 필요하다면 — 근거는 용량이 아니라 **직렬화**다

정본 §8이 지적하는 관통 제약: **모든 타임스탬프가 직렬화 배리어**다. 세밀한 zone은 GPU 오버랩을 죽여 파이프라이닝을 파괴한다. 즉 깊이를 제한할 이유가 있다면 **"풀이 모자라서"가 아니라 "측정이 측정 대상을 바꿔서"** 다.

그건 **숫자로 정할 수 있는 값이 아니다** — 씬·백엔드·GPU에 따라 다르다. 정본이 이미 정한 설계 원칙이 옳다: **패스 경계급으로 제한.** 그리고 §2에서 보듯 **RD가 이미 그것을 강제**하고 있다.

⇒ **권고: 깊이에 숫자 상한을 도입하지 않는다.** 도입하면 (a) 존재하지 않는 제약을 강제하고 (b) 초과 처리 정책이라는 새 실패 경로를 만들며 (c) 실제 제약(총 개수)은 이미 처리되어 있다.

### 그럼에도 상한을 넣기로 한다면 — **1회 경고 후 평탄화**
`>` 깊이가 N을 넘으면 그 스코프를 부모 깊이로 **평탄화**(트리에서 형제로 붙임)하고 **프레임당 한 번 경고**한다. 근거:
- **명시적 실패는 과하다.** 프로파일링은 진단 도구다. 깊이 초과로 프레임을 죽이면 정작 진단하려던 문제에 도달하지 못한다.
- **조용한 절단은 금지**(지시대로). 스코프를 버리면 그 안의 시간이 어디에도 안 잡혀 **합계가 조용히 틀어진다** — §1의 무진단 실패와 같은 형태다.
- **평탄화는 시간을 보존한다.** 계층 정보만 잃고 측정값은 남는다. 그리고 경고가 그 사실을 알린다.
- 프레임당 1회로 제한하는 이유: 깊이 초과는 프레임마다 반복되므로 매 발생 경고는 로그를 덮는다.

---

## 5. 이 판정이 틀렸을 조건

| 판정 | 틀릴 수 있는 조건 |
|---|---|
| "256이 넉넉하다" | **박스 24개짜리 씬 1개 측정.** Nanite/Lumen 오픈월드 프레임은 패스 수와 광원별 그림자 패스가 훨씬 많다. **G4(전면 deferred) 이후 재측정 필요.** 다만 초과해도 §1이 큰 소리로 실패하고 설정으로 올릴 수 있다 |
| "깊이는 제약되지 않는다" | 현재 `depth`/`parent`가 `int32_t`이고 에디터 트리에 제한이 없다는 사실 기준. 소비자가 늘면 달라질 수 있다 |
| "패스 경계 제한이 Metal과 정합" | `capture_timestamp` 가드 기준. **드라이버 레벨 `command_timestamp_write`는 여전히 임의 지점을 허용**하므로, RD 밖에서 드라이버를 직접 쓰는 경로가 생기면 무너진다 |
| Vulkan 8태스크 / Metal 0 | 각 백엔드 1회 측정, 동일 씬. 프레임 간 편차는 재지 않았다 |
| "총 개수만이 제약" | **프레임 지연 readback은 측정하지 않았다.** RD 스파이크에서 async 리드백 왕복이 `frame_queue_size - 1`임을 쟀지만, **타임스탬프 결과 readback 경로는 별개**이고 이번에 재지 않았다 — 풀이 프레임 슬롯마다 하나씩이라 슬롯 재사용 전에 결과가 나와야 한다 |

---

## 6. 미검증 / 다음

- **타임스탬프 결과 readback의 프레임 지연** — 미측정(§5). 풀이 프레임당 하나라 지연이 `frames.size()`를 넘으면 슬롯이 재사용되며 결과가 덮인다. 확인 필요.
- **G4 이후 실제 패스 수 재측정** — 256 여유 재확인.
- **`QueryPoolID(1)` 가짜 성공 ID** — Metal 리포트 §5의 후속. 능력 질의(`SUPPORTS_TIMESTAMP_QUERY`)를 안 보고 호출하는 코드는 여전히 조용히 0을 받는다. **P3에 "미구현이면 실패 반환" 항목으로 남긴다.**

---

*작성: 2026-08-25 · L9 / godot-contributer-3 · 측정: Apple M2 Pro/macOS(Metal) · NVIDIA GB10/Linux(Vulkan)*
