# G1 — 통합 GBuffer 스키마 v1 동결 제안 (RFC)

> **상태: 초안(RFC) — 2026-08-25.** 로드맵 게이트 G1(M4)의 동결 대상 문서.
> 근거 정본: [unified-gbuffer 리서치](./godot-unified-gbuffer-aov-research.md) §2 Tier 모델. 이 문서는 리서치를 **동결 가능한 계약**으로 구체화한 것.
> **의견 수렴: C1(L1 리졸브) · C2(L2 vis-buffer 소비) · C3(L3 ReSTIR 입력) — 3인 서명 후 G1 동결.**

## 1. 동결되는 것 / 동결되지 않는 것

| 동결 (변경 시 4레인 재작업) | 비동결 (레인 내부 자유) |
|---|---|
| Tier-1 어태치먼트 목록·포맷·채널 시맨틱 | 어태치먼트의 내부 생성 경로 (래스터 vs vis-buffer 리졸브) |
| `gb_objectid` 네임스페이스 규약 (§3) | Tier-2 export 채널 추가 (게이트 플래그 뒤라 자유) |
| 노멀 인코딩 (RG16 octahedral) | Tier-3 라이팅 분해 write-out 목록 |
| depth 시맨틱 (R32F view-Z, `-vertex.z`) | 프리패스/컬링용 내부 버퍼 |
| `RB_SCOPE_GBUFFER` 스코프 이름 | |

## 2. Tier-1 스키마 (동결 제안)

| # | RB tex | 포맷 | 채널 | 소비자 |
|---|--------|------|------|--------|
| 0 | `gb_albedo` | RGBA8 unorm (HDR 머티리얼 시 RGBA16F 승격 옵션 — **v1은 RGBA8 고정**) | albedo.rgb + alpha | 리졸브·AOV |
| 1 | `gb_normal` | **RG16 unorm, octahedral** | world-space normal | 리졸브·ReSTIR 재투영·SSR·AOV |
| 2 | `gb_orm` | RGBA8 unorm | ao / roughness / metallic / sss-mask | 리졸브·ReSTIR·AOV |
| 3 | `gb_emission` | RGBA16F | emission.rgb (+a 예약) | 리졸브·AOV |
| 4 | `gb_depth` | R32F | view-space Z (`-vertex.z`) | 위치 재구성·전 소비자 |
| 5 | `gb_objectid` | **R32_UINT** | §3 네임스페이스 | Nanite 리졸브·Cryptomatte·RT 히트 매칭 |
| 6 | `gb_motion` | RG16F | screen-space motion | TAA·ReSTIR·AOV (기존 `motion_vector` 재사용) |

- 합계 대역폭(1080p, v1): ≈ 4+4+4+8+4+4+4 = 32 B/px ≈ 66 MB. 4K ≈ 265 MB. Tier-2는 export 시에만.
- shading-model-ID: v1에서는 `gb_orm.a`(sss-mask)와 별개 비트필드가 필요해지는 시점(라이트맵/SH deferred 편입)에 **`gb_objectid` 상위 비트가 아니라 신규 R8_UINT 어태치먼트로 추가**한다 — objectid 네임스페이스 오염 금지. v1은 미포함.

## 3. `gb_objectid` 네임스페이스 (동결 제안)

**단일 32-bit 인스턴스 ID 공간을 3소비자가 공유한다:**
- 비-Nanite opaque: `InstanceData.object_id` (`scene_forward_clustered_inc.glsl:27`에 안정 ID 신설)
- Nanite: vis-buffer `instanceId` 필드에서 리졸브 시 기록 — **같은 ID 공간**
- RT(TLAS): `AccelerationStructureGeometry` 인스턴스 `uint32_t id`(`rendering_device.h:1379`)에 **동일 값** 배선 → 래스터 픽셀과 RT 히트가 같은 머티리얼 조회
- 예약값: `0xFFFFFFFF` = "no object" (sky/클리어). Cryptomatte 해시는 이 ID에서 유도(Tier-2).

## 4. v1에서 의도적으로 뺀 것 (제기되면 기각 근거)

| 요구 | 기각 근거 | 재개봉 조건 |
|---|---|---|
| anisotropy/clearcoat 채널 | Tier-2 (Substrate식 비트스트림)로 — Tier-1 대역폭 보호 | 해당 머티리얼이 opaque 대다수가 될 때 |
| world-position 채널 | depth 재구성으로 충분 (전 엔진 공통) | 없음 |
| 라이팅 분해(diffuse/spec AOV) | GBuffer가 아니라 리졸브 패스 write-out (Tier-3) | 없음 — 구조적 |
| RGBA16F albedo 기본 | 대역폭 2배. 베이크·AOV는 8bit로 충분 | HDR albedo 워크플로 실증 시 |

## 5. 서명란

| 레인 | 담당 | 판정 | 비고 |
|---|---|---|---|
| L1 (리졸브·FB 배선) | C1 | ⬜ 대기 | |
| L2 (vis-buffer → 리졸브) | C2 | ⬜ 대기 | |
| L3 (ReSTIR/RT 입력) | C3 | ⬜ 대기 | |
| 메인테이너 | ✅ 제안 | | |

3인 승인 시 이 문서가 **G1 동결본**이 되고, 이후 변경은 4레인 합의 + 로드맵 개정을 요구한다.
