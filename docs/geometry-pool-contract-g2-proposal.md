# G2 — 지오 풀 버퍼 계약 동결 제안 (RFC)

> **상태: v1.1 (2026-08-25) — L2 실측 입력 반영 (DenseGrid 51k tri·UVSphere 36k tri 픽스처).** 로드맵 게이트 G2(M6)의 동결 대상 문서.
> 근거: [nanite-impl](./godot-nanite-implementation-research.md) §10.4-3 · [nanite-perf](./godot-nanite-performance-implementation-research.md) §2.5-R2 · [G1 RFC](./gbuffer-schema-v1-proposal.md) §4.2 · C3 RD 스파이크(`rd-capability-spike-report.md`).
> **의견 수렴: C1(L1) · C2(L2, 풀 생산자) · C3(L3-대행, BLAS 소비자) — 3인 서명 후 G2 동결.**
> G1과의 관계: G1은 *픽셀 쪽* 계약(GBuffer), G2는 *지오메트리 쪽* 계약(풀·클러스터·BLAS). 접점은 `gb_objectid` 24-bit와 §4.2 정점 속성 요구.

## 1. 동결되는 것 / 동결되지 않는 것

| 동결 | 비동결 (레인 내부 자유) |
|---|---|
| 풀 버퍼 생성 플래그 집합 (§2) | 풀 내부 할당자 전략(버디/링/세그리게이트) |
| 정점 레코드 필수 속성·정밀도 (§3) | 압축 인코딩(meshopt codec 채택 여부·양자화 비트) — 단 §3의 *디코드 후* 보장은 유지 |
| 클러스터 레코드 필수 필드 (§4) | 레코드의 SoA/AoS 배치 |
| ID 폭 계약 (§5 — G1 §3과 정합) | 스트리밍 페이지 크기·상주 정책 (S5에서 확정) |
| BLAS 빌드 입력 경로 (§6) | BLAS 리빌드/리핏 스케줄링 |

## 2. 풀 버퍼 생성 계약

**SSBO로도 바인딩 가능한 단일 대형 정점/인덱스 버퍼 풀** (bindless 부재 확정 — RD 스파이크 ⓒ FAIL — 이므로 단일 버퍼 + 오프셋/BDA가 유일 경로이자 성능상으로도 정석):
- **생성 진입점 동결 (2026-08-25 C3 dgx 실측 — 플래그보다 바꾸기 어려운 계약):** 정점 풀은 **`vertex_buffer_create`**, 인덱스 풀은 **`index_buffer_create`** 로 생성한다. `storage_buffer_create`로 만든 버퍼는 **BLAS 입력이 될 수 없다** — `blas_create`(`rendering_device.cpp:302`)가 `vertex_buffer_owner`(`:325`)/`index_buffer_owner`(`:344`)만 조회한다("Parameter vertex_buffer is null"로 거부, 실측). 역방향은 성립: `uniform_set_create`의 STORAGE_BUFFER 분기(`:4743-4761`)는 `BUFFER_USAGE_STORAGE_BIT`가 있으면 vertex/index 버퍼도 받는다.
- 생성 플래그: `BUFFER_CREATION_AS_STORAGE_BIT | BUFFER_CREATION_DEVICE_ADDRESS_BIT | BUFFER_CREATION_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT` — 이 조합으로 SSBO 읽기·BDA·AS 빌드 입력 3용도 전부 성립(dgx 실측 3줄: blas OK · device_address OK · storage 바인딩 OK). 스트리밍 업로드(`buffer_update`)는 기본 지원. **나중에 플래그를 바꾸면 스트리밍 레이어 전체 개조**(nanite-impl §10.4-3) — 이 목록이 G2의 존재 이유다.
- 크기 상한 질의: `LIMIT_MAX_STORAGE_BUFFER_SIZE`(= 49, 훅 ③ 신설) 사용. 1GiB 가정 금지(훅 ③에서 과대평가 확인됨). ⚠️ 풀이 정점 버퍼로 생성되므로 이 상한이 vertex buffer에 동일 적용되는지 L1 확인 항목(C3 지적).
- 업로드 경로: `buffer_update` 부분 갱신. 리드백(요청 페이지)은 async 링 — 예산은 다운로드 스테이징 분리 노브(Task #19) 확정 후 산정. **왕복 2~3프레임(멀티스레드) 전제.**

## 3. 정점 레코드 (디코드 후 보장)

| 속성 | 정밀도 (디코드 후) | 근거 |
|---|---|---|
| position | float3 (클러스터 로컬 양자화 허용, 복원 오차는 클러스터 LOD 오차 예산 내) | 래스터·BLAS 공용 |
| **normal** | oct RG16 등가 이상 | G1 §4.2 — 리졸브 셰이딩 필수 |
| **UV(0)** | float2 등가 (반정밀 허용, 텍셀 정확도 보장) | G1 §4.2 — 머티리얼 평가 필수 |
| ~~tangent~~ | **저장 금지** | G1 §4.2 — 리졸브가 3정점 위치+UV에서 해석적 유도 |
| UV1(라이트맵)·컬러·스킨 가중치 | v1 스코프 외 — 필요 시 확장 슬롯(§7) | 스태틱 메시 우선 |

**레이아웃 동결 (v1.1, C2 실측):** **(b) 공유 정점 버퍼 + 클러스터별 슬라이스 + 8-bit 로컬 인덱스.** 실측 중복 계수 2.6~2.8×에서도 32-bit 전역 인덱스(a) 대비 **37% 작음**(Grid: 1,073 vs 1,704 KiB) — "슬라이스가 손해"라는 직관이 틀리는 지점. 완전 복제(c)는 (b)보다 53% 큼.
**`max_cluster_vertices` = 255 동결** — 실측에서 구의 레벨 0이 정확히 255에 도달(8-bit 로컬 인덱스 0..254 적합). 256으로 올리면 카운트 필드가 9-bit를 요구하므로 이 상한은 계약이다.

현 S1 DAG는 위치만 보존 → **G2 서명 시점에 L2가 노멀+UV 보존으로 확장** (C2 사전 합의됨).

## 4. 클러스터 레코드 필수 필드

vis-buffer 27b 클러스터 ID로 인덱스되는 레코드가 최소 보유할 것:
- 정점/인덱스 풀 오프셋 + 카운트 (트라이앵글 ≤128 — 7b 계약, S1 테스트가 강제 중)
- **LOD 오차 (절대 메시 단위)** — G1 §4.1 모션/히스토리 잔차 상한의 원천. 리졸브가 화면 투영해 전달.
  **의미 규정 (v1.1 명확화):** 이 값은 **기하 편차**의 상한이며 속성(노멀/UV) 오차를 포함하지 않는다. 단순화기가 보고하는 결합 쿼드릭 오차를 그대로 쓰면 안 된다 — 그것은 거리가 아니다. (근거: L2 실측 — 동일 지오메트리 레벨 1~7에서 실측 기하 편차가 meshopt 결합 보고값을 초과, 즉 결합값은 상한도 하한도 아님. 속성 메트릭은 collapse 순서 결정에만 사용하고, 저장·투영용 값은 생존 정점 대비 제거 정점의 최대 거리를 측정해 삼각부등식으로 누적할 것.)
- 컬링 바운드: 스피어 + 노멀 콘 (`meshopt_computeClusterBounds`)
- **그룹 참조 2개 (v1.1 정정 — 단수로는 컷 식이 성립 불가):** `source_group`(자신을 만든 그룹 → `error`/`lod_bounds`) + `parent_group`(자신을 대체할 그룹 → `parent_error`/`parent_lod_bounds`). 런타임 컷 판정 `error <= t < parent_error`가 양쪽을 요구. **루트 센티넬: `parent_group = 0xFFFFFFFF` → `parent_error = +inf`** — 없으면 루트가 원거리에서 사라진다.
- **레코드 배치: B(그룹 간접) 채택** — 오차·LOD 구는 그룹 속성이라 자기완결(A)은 그룹당 평균 7.8회 중복(실측 65 vs 35 KiB). 그룹 테이블 전체 ~2.5KiB로 L1 캐시 상주. 단 S2/S3 컬링 셰이더가 종속 로드 2회를 기피하면 A로 전환 가능 — 전환 시 이 절 개정 필요(Nanite/Bevy는 A)
- **instance 참조는 없음** — instance ID는 *가시 클러스터 리스트*(프레임 산출물)에서 옴 (G1 §3 역참조 구조)

## 5. ID 폭 계약 (G1 정합 재확인)

- instance ID: **24-bit** (G1 §3), 상한 2²⁴-2, 발급기 에러 + `DEV_ASSERT`
- 클러스터 ID: **27-bit** (프레임당 가시 1.34억), 트라이앵글: **7-bit**
- Metal 폴백 변종: 32-bit vis-buffer(HW depth 활용) = 25b 클러스터 + 7b 트라이앵글 — **클러스터 상주 상한이 25b로 줄어드는 것을 스트리밍 예산이 수용해야 함** (C2 §⑤ 검토 기반)

## 6. BLAS 직접 빌드 경로 (L3 소비)

`AccelerationStructureGeometry`는 원시 `vertex_buffer`/`index_buffer` RID + offset/stride/format을 받으므로(**Mesh RID 불요**), **풀에서 DAG 특정 LOD 레벨을 잘라 자료 중복 없이 BLAS 빌드**한다 — UE의 별도 fallback mesh 대비 구조적 우위. 요구:
- §2 플래그 준수 (AS-build 입력)
- BLAS용 LOD 선택 정책: v1은 **고정 LOD 레벨**(예: 레벨 2~3) — 프레임별 컷과 무관하게 안정. 리핏/리빌드 스케줄은 비동결
- **position 양자화 직접 입력: 가능 (C3 코드+스펙 실측, 2026-08-25 — 단 실기 미검증):** 디코드 스테이징 불요. Vulkan AS 필수 지원 포맷에 `R16G16B16A16_SNORM`이 있어 클러스터 로컬 양자화를 직접 입력. **제약 2건 동결:** ① 3성분 16b 포맷은 필수 목록에 없음 → **정점 레코드 position은 4성분 8B/정점(4번째 패딩)** — L2 크기 산정 반영 ② 10:10:10:2 등 추가 패킹은 런타임 `bufferFeatures` 조회 선행 필요한데 **그 코드가 현재 0건**(Godot은 AS 정점 포맷 무검증 — 미지원 포맷은 검증레이어/UB로 남). ✅ **실기 검증 완료 (2026-08-25, dgx/GB10):** Godot `blas_create`→`blas_build` 경로로 float32x3·snorm16x4(8B)·snorm16x3(6B) 전부 통과. **계약 동결 = `R16G16B16A16_SNORM` 8B**, 6B(R16G16B16_SNORM)는 스펙 필수 아님·NVIDIA 확인뿐이라 벤더별 최적화로 비동결

## 7. 확장 슬롯

정점 레코드에 **버전 필드 + 가변 속성 블록** 예약 — UV1/컬러/스킨은 레코드 버전 증가로 추가하되 §3 필수 속성의 오프셋은 불변. 이것으로 "확장 = 재작업"을 방지.

## 7.5 검증 항목 (서명 전/후 추적)

- **크로스 플랫폼/컴파일러 임포트 결정성 (⚪ 미검증, C2):** meshopt QEM은 부동소수라 FMA 계약·최적화 수준에 따라 collapse 순서가 갈릴 수 있음. 같은 바이너리·같은 머신에서는 프로세스 간 결정성 실측 확인(3회 바이트 동일). 팀원 간 임포트 캐시 일관성에 직결되므로 **Linux/Windows 러너 확보 시 1순위 검증** — 그때까지 "결정적"이라 기록하지 말 것.
- **스트리밍 원자 단위 = 그룹 (v1.1 채택, C2 실측):** 그룹 크기 실측 2~10, 두 픽스처 평균 공히 7.8 — 그룹 1개 ≈ 890 tri ≈ (b) 레이아웃 ~5 KiB. 편차가 좁아 할당자 친화적. §2 페이지 정책(비동결)의 기본 가정으로 채택.
- **G1 §5의 Metal 25-bit 캡 캐비엇 해제:** 실측 밀도(16.3 클러스터/1,000tri) 기준 2²⁵ ≈ 상주 삼각형 20.6억 — 실무 제약 아님.
- **디코드 스테이징:** C3 검증으로 양자화 직접 입력이 성립해 기본 경로에서 불요. 단 어떤 이유로든 스테이징 버퍼를 쓰게 되면 **그 버퍼도 §2 생성 계약(진입점+플래그) 대상**이다.

### 7.6 검증 코드 배치 (v1.1, C2 제안 채택)

- **생산자 고유 불변식** (tri≤128·vert≤255·그룹 참조 상호정합·오차 단조성·수밀 컷): `modules/nanite/tests/` — 생산자가 소유.
- **레인 간 계약** (§2 진입점/플래그·BDA·AS-build·ID 폭 24/27/7·센티넬): **어느 모듈에도 속하지 않는 공유 계약 테스트** `tests/servers/rendering/test_geometry_pool_contract.h` — 계약 변경이 한 레인의 로컬 수정처럼 보이지 않게.
- **디바이스 의존 항목** (BDA·AS-build·아토믹): headless에서 **조용히 통과 금지** — "skipped: no rendering device"를 명시 출력. 초록불이 "검증됨"으로 오독되는 것을 방지.

## 8. 서명란

| 레인 | 담당 | 판정 | 비고 |
|---|---|---|---|
| L1 (버퍼 생성·업로드 경로) | C1 | ⬜ | §2 플래그 실현성 (RD 레벨) |
| L2 (풀 생산자·DAG 포맷) | C2 | ✅ **승인/서명 (2026-08-25, v1.1)** | §3(b)·255 동결·§4 이중그룹·§7.5/7.6 반영 확인. 부수: §4의 `meshopt_computeClusterBounds` 미컴파일 발견 → 메인테이너가 SCsub 수정(1d1dbf5f31) |
| L3-대행 (BLAS 소비) | C3 | ✅ **승인/서명 (2026-08-25)** | §6 실기 검증 + §2 생성 진입점 블로커 제기·반영(조건부→서명). 근거: c3/lumen-spike 73fde51b48, misc/rt_spike/ 재현 가능 |
| 메인테이너 | ✅ 제안 | | |
