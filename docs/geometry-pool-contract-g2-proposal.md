# G2 — 지오 풀 버퍼 계약 동결 제안 (RFC)

> **상태: v1.0 초안 (2026-08-25).** 로드맵 게이트 G2(M6)의 동결 대상 문서.
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

**단일 대형 SSBO 풀** (bindless 부재 확정 — RD 스파이크 ⓒ FAIL — 이므로 단일 버퍼 + 오프셋/BDA가 유일 경로이자 성능상으로도 정석):
- 생성 usage: `STORAGE`(컬링/리졸브 읽기) + **BDA**(`buffer_get_device_address`) + **AS-build 입력**(acceleration structure build input — Vulkan `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY`) + `TRANSFER_TO`(스트리밍 업로드). **나중에 플래그를 바꾸면 스트리밍 레이어 전체 개조**(nanite-impl §10.4-3) — 이 목록이 G2의 존재 이유다.
- 크기 상한 질의: `LIMIT_MAX_STORAGE_BUFFER_SIZE`(= 49, 훅 ③ 신설) 사용. 1GiB 가정 금지(훅 ③에서 과대평가 확인됨).
- 업로드 경로: `buffer_update` 부분 갱신. 리드백(요청 페이지)은 async 링 — 예산은 다운로드 스테이징 분리 노브(Task #19) 확정 후 산정. **왕복 2~3프레임(멀티스레드) 전제.**

## 3. 정점 레코드 (디코드 후 보장)

| 속성 | 정밀도 (디코드 후) | 근거 |
|---|---|---|
| position | float3 (클러스터 로컬 양자화 허용, 복원 오차는 클러스터 LOD 오차 예산 내) | 래스터·BLAS 공용 |
| **normal** | oct RG16 등가 이상 | G1 §4.2 — 리졸브 셰이딩 필수 |
| **UV(0)** | float2 등가 (반정밀 허용, 텍셀 정확도 보장) | G1 §4.2 — 머티리얼 평가 필수 |
| ~~tangent~~ | **저장 금지** | G1 §4.2 — 리졸브가 3정점 위치+UV에서 해석적 유도 |
| UV1(라이트맵)·컬러·스킨 가중치 | v1 스코프 외 — 필요 시 확장 슬롯(§7) | 스태틱 메시 우선 |

현 S1 DAG는 위치만 보존 → **G2 서명 시점에 L2가 노멀+UV 보존으로 확장** (C2 사전 합의됨).

## 4. 클러스터 레코드 필수 필드

vis-buffer 27b 클러스터 ID로 인덱스되는 레코드가 최소 보유할 것:
- 정점/인덱스 풀 오프셋 + 카운트 (트라이앵글 ≤128 — 7b 계약, S1 테스트가 강제 중)
- **LOD 오차 (절대 메시 단위)** — G1 §4.1 모션/히스토리 잔차 상한의 원천. 리졸브가 화면 투영해 전달
- 컬링 바운드: 스피어 + 노멀 콘 (`meshopt_computeClusterBounds`)
- 그룹 ID (DAG 컷 판정용 — 오차·바운드는 그룹 단위 단조)
- **instance 참조는 없음** — instance ID는 *가시 클러스터 리스트*(프레임 산출물)에서 옴 (G1 §3 역참조 구조)

## 5. ID 폭 계약 (G1 정합 재확인)

- instance ID: **24-bit** (G1 §3), 상한 2²⁴-2, 발급기 에러 + `DEV_ASSERT`
- 클러스터 ID: **27-bit** (프레임당 가시 1.34억), 트라이앵글: **7-bit**
- Metal 폴백 변종: 32-bit vis-buffer(HW depth 활용) = 25b 클러스터 + 7b 트라이앵글 — **클러스터 상주 상한이 25b로 줄어드는 것을 스트리밍 예산이 수용해야 함** (C2 §⑤ 검토 기반)

## 6. BLAS 직접 빌드 경로 (L3 소비)

`AccelerationStructureGeometry`는 원시 `vertex_buffer`/`index_buffer` RID + offset/stride/format을 받으므로(**Mesh RID 불요**), **풀에서 DAG 특정 LOD 레벨을 잘라 자료 중복 없이 BLAS 빌드**한다 — UE의 별도 fallback mesh 대비 구조적 우위. 요구:
- §2 플래그 준수 (AS-build 입력)
- BLAS용 LOD 선택 정책: v1은 **고정 LOD 레벨**(예: 레벨 2~3) — 프레임별 컷과 무관하게 안정. 리핏/리빌드 스케줄은 비동결
- position 양자화 시 BLAS 입력은 **디코드된 float3 스테이징** 또는 양자화 포맷 직접 지원 여부 확인 (⚪ 미검증 — 서명 전 C3 확인 항목)

## 7. 확장 슬롯

정점 레코드에 **버전 필드 + 가변 속성 블록** 예약 — UV1/컬러/스킨은 레코드 버전 증가로 추가하되 §3 필수 속성의 오프셋은 불변. 이것으로 "확장 = 재작업"을 방지.

## 8. 서명란

| 레인 | 담당 | 판정 | 비고 |
|---|---|---|---|
| L1 (버퍼 생성·업로드 경로) | C1 | ⬜ | §2 플래그 실현성 (RD 레벨) |
| L2 (풀 생산자·DAG 포맷) | C2 | ⬜ | §3 확장 + §4 레코드 |
| L3-대행 (BLAS 소비) | C3 | ⬜ | §6 — 양자화 입력 검증 포함 |
| 메인테이너 | ✅ 제안 | | |
