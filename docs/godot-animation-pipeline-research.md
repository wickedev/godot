# Godot 4.x에 원신급 고급 애니메이션 파이프라인 구현 — 딥리서치 & 기술 실측

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) 의 "6. 애니메이션 고급 파이프라인 ⚠️" 항목을 실제 구현 관점에서 딥다이브한 문서.
>
> **결론 요약:** §6의 초판 진단("AnimationTree 블렌딩이 단순 / 리타게팅 제한적 / 모션매칭·컨트롤릭 없음")은 **Godot 4.8-dev 소스트리 실측 결과 상당 부분 정정이 필요하다.** 4.8-dev에는 이미 (a) **커스텀 애니 노드용 공식 확장 훅(`AnimationNodeExtension`, 4.4 머지)**, (b) **런타임 리타게팅 노드(`RetargetModifier3D`)**, (c) **대규모 IK/제약 스위트(CCDIK·FABRIK·Jacobian·Spline·TwoBone·Aim·CopyTransform·SpringBone…)** 가 들어와 있다. **모션 매칭은 순수 GDExtension으로 구현 가능하며, 이미 동작하는 커뮤니티 애드온(`godot-motion-matching`)이 존재한다.**
>
> 진짜로 남은 격차는 4개다: ① **인러셜라이제이션(inertialization) 부재** — 블렌딩이 전부 크로스페이드, ② **레이어드/애디티브 + 본마스크 스테이트머신 부재** — 동시 2상태 블렌드만, ③ **완전한 런타임 리타게팅 패리티 미달** — 트랙이 스켈레톤 경로/이름에 바인딩, ④ **군중 스키닝 인프라 부재** — 애니 LOD·인스턴싱·MultiMesh 스켈레톤 전무. 이 넷 중 ①②는 GDExtension으로, ③④는 대체로 코어 작업이거나 오프라인 우회가 필요하다.
>
> **근거:**
> - **외부 딥리서치 하니스** (113 에이전트, 6각도, 30 소스 페치, 130 주장 추출 → 25 주장 적대적 3표 검증 → **25/25 confirmed, 0 refuted**). 1차 출처: Clavet GDC2016, Holden et al. LMM SIGGRAPH2020, Bollo GDC2018, Epic/Unity/Godot 공식 문서, Godot PR/proposal.
> - **로컬 소스 실측** (`/Users/ryan/Workspace/godot` = Godot 4.8-dev 트리, file:line 직접 확인).
> - 아래 각 주장에 **[DR n-0]**(딥리서치 검증표) 또는 **`file:line`**(로컬 실측) 근거를 명시.

---

## 0. 한 장 요약

| 기능 | 원신/AAA 방식 | Godot 4.8-dev 현황 | 구현 위치 · 난이도 |
|------|---------------|---------------------|--------------------|
| **모션 매칭** | mocap DB 최근접이웃 검색(27D 피처) + KD-tree | ❌ 엔진 미탑재. 단 **훅은 있음** + **애드온 존재** | 🟢 **순수 GDExtension** (`AnimationNodeExtension`, `godot-motion-matching`) |
| **Learned MM(신경망)** | Decompressor/Stepper/Projector(512유닛)로 상수 메모리·70배 절감 | ❌ 없음 (ML 인프라도 전무) | 🟢 GDExtension 이식 가능 — 자체 MLP 커널 or ONNX 링크 |
| **컨트롤 릭 / 절차적 IK** | UE Control Rig, Unity Animation Rigging | ✅ **대규모 IK/제약 스위트 이미 존재** | 🟢 빌트인 + `SkeletonModifier3D` 커스텀(GDExtension) |
| **런타임 리타게팅** | UE Retarget Pose From Mesh, Unity Avatar | 🟡 **부분** — `RetargetModifier3D` 존재, 단 트랙-경로 바인딩 한계 | 🟡 부분 GDExtension / 완전 패리티는 코어 |
| **인러셜라이제이션** | Bollo 5차 최소저크 포스트프로세스 | ❌ 없음 — 전부 크로스페이드 | 🟢 GDExtension(`SkeletonModifier3D` 후처리) |
| **레이어드/애디티브 + 본마스크 SM** | UE/Unity 애니 레이어 | ⚠️ Add2/Sub2·필터만, 스테이트머신 레이어 없음 | 🟡 GDExtension 커스텀 노드/모디파이어 |
| **군중 스키닝(수천 캐릭터)** | GPU 팔레트 스키닝 + VAT + 애니 LOD | ❌ 애니 LOD·인스턴싱·MultiMesh 스켈레톤 없음 | 🔴 코어 or 오프라인 VAT 우회 |

**타깃 하드웨어:** Forward+ / 데스크톱·모바일. 스키닝은 RD 백엔드 GPU 컴퓨트 전용(`skeleton.glsl`), CPU 폴백 없음.

---

## 1. 모션 매칭 (Motion Matching) — 🟢 순수 GDExtension 가능, 애드온 실재

### 1.1 알고리즘 (검증됨)

런타임에 **매 ~N프레임(보통 10)**, mocap 포즈 데이터베이스에서 **현재 포즈 + 원하는 미래 궤적(trajectory/plan)** 양쪽에 대한 **가중 제곱유클리드 거리를 최소화**하는 프레임을 찾아 **짧은 블렌드 타임으로 전이**한다. DB는 개별 클립을 전이 그래프로 저작하는 대신 **5~10분짜리 비구조화 mocap을 통째로 검색가능 DB로 임포트**해서 만든다.

> Clavet GDC 2016 원문: "continuously find the frame in the mocap database that simultaneously matches the current pose and the desired future plan, and transition with a small blend time", "5 or 10 minutes of a person running around". Holden et al. LMM(SIGGRAPH 2020)이 가중 L2 매칭 비용·주기적 재검색 확인. **[DR 3-0]** (Clavet GDC2016, LMM 논문)

### 1.2 피처/궤적 스키마 (검증됨)

캐노니컬 LMM 공식의 프레임당 **27차원** 피처벡터 `x = {tt, td, ft, ḟt, ḣt}`:
- 미래 2D 궤적 위치 20/40/60프레임 앞(=0.33/0.66/1.0초 @60Hz) → R6
- 궤적 페이싱 방향 → R6
- 양발 관절 위치 → R6
- 양발 속도 → R6
- 힙 속도 → R3

전부 캐릭터 로컬 공간. **스키마는 설정 가능**하며(UE Pose Search 채널은 다름 — Position/Velocity/Rotation/Phase) 이건 시작 템플릿이지 고정 스펙이 아니다.

> LMM 논문 verbatim, 차원 산술 검증(6+6+6+6+3=27). O3DE 문서가 20/40/60프레임 독립 확인. **[DR 3-0]**

**로컬 실측 — Godot에서 피처 추출은 가능한가? ✅**
- 트랙 샘플링이 전부 GDExtension에 바인딩됨: `position_track_interpolate`/`rotation_track_interpolate`/`scale_track_interpolate`/`blend_shape_track_interpolate`/`value_track_interpolate`/`bezier_track_interpolate` (`scene/resources/animation.cpp:3984-4028`). → 오프라인 피처 DB 빌드와 런타임 포즈 피처 계산 모두 스크립트/확장에서 수행 가능.

### 1.3 가속 구조 & 비용 모델 (검증됨)

DB 전체 브루트포스는 느려서 **가속 구조 필수**: KD-Tree(Clavet 2016), 복셀 룩업(Büttner 2018), 클러스터링, 2계층 AABB BVH(16/64프레임 그룹). **UE는 PCA 투영 KD-tree(기본)** + Brute Force + 실험적 VPTree로 구현하며 `KDTree Max Leaf Size`/`Number Of Principal Components`/`KNNQueryNumNeighbors`를 튜닝한다.

> LMM 논문 + Epic UE 문서 verbatim. **[DR 3-0]**

**로컬 실측 — 가속 구조는 자체 구현 필수.**
- 리포지토리 전역 검색 결과 `motion_matching`/KD-tree/nearest-neighbor/ozz/ACL **애니메이션 관련 코드 전무**. (`thirdparty/meshoptimizer`의 "nearest"는 메시 클러스터링용, 무관.) → **DB·피처벡터·KD-tree 전량 확장에서 구현.**

### 1.4 메모리 스케일링 & Learned MM (검증됨)

표준 MM의 메모리(및 일부 런타임)는 **애니 데이터 양에 선형 비례** → 애니 다양성 vs 메모리 예산의 영구 트레이드오프. 구체 수치: 1시간 30Hz DB = 108,000프레임(59피처 기준 ~24MB), ~600MB MM 컨트롤러가 LMM으로 **~9MB**까지 압축.

**Learned Motion Matching**은 MM을 이산 단계로 분해해 신경망으로 대체: **Decompressor**(피처→풀포즈, 3층) + **Stepper**(다음 프레임 피처 예측, 4층) + **Projector**(쿼리벡터에서 검색/룩업 에뮬레이트, 6층). **각 512 히든유닛, 잠재 z∈R³²** (논문 Table 1). Compressor는 런타임 불필요. 최종 런타임 모델은 **애니 데이터·매칭 메타데이터를 전혀 저장하지 않아** 생성모델처럼 스케일링(고정 가중치)하면서 MM 거동을 보존.

> LMM 논문 Table 1 verbatim(3/4/6층·512유닛·z∈R³²), abstract "no need to store animation data or additional matching meta-data in memory". **[DR 3-0]** (LMM 논문, Ubisoft La Forge, orangeduck·pau1o-hs 참조구현)

**메모리·비용 실측치(검증됨):**
- **상수 CPU 비용**(DB 크기 무관) + **~10~70배 메모리 절감**: Bear 995.6MB→7.1MB, Locomotion 52.1MB→5.3MB, Ubisoft 590MB→**8.5MB**(16bit 양자화 시 "70배"). "원본 MM을 거의 완벽히 에뮬레이트". **[DR 3-0]**
- ⚠️ **트레이드오프**: 메모리 절감의 대가로 **프레임당 신경망 추론 계산이 추가**(MIG 2023 후속논문이 LMM이 클래식 대비 계산비용↑ 지적).
- **실시간성**: 논문이 **단일 스레드 1 CPU**(Xeon 3.5GHz)에서 커스텀 최적화 추론 라이브러리로 측정, **Stepper+Decompressor는 매 프레임, 큰 Projector는 N=10프레임마다만** 실행. Ubisoft 프로덕션 출하 + Unity(ONNX+Barracuda)·orangeduck C++로 시연. **[DR 3-0]**

### 1.5 Godot 구현 가능성 — 🟢 순수 GDExtension, 코어 포크 불요

**핵심 반전(딥리서치):**
- Godot가 **`AnimationNodeExtension`을 4.4에 머지**(PR **#99181**, 2024-12-13 머지, proposal **#11123**은 *명시적으로 모션 매칭을 타깃*). 개발자가 가상 `_process_animation_node`를 오버라이드해 커스텀 애니 노드 로직을 `AnimationTree`에 통합 가능. **[DR 3-0]**
- **이미 동작하는 구현 존재**: `GuilhermeGSousa/godot-motion-matching`(~225 스타, CI 통과, Asset Library #3822, C++/godot-cpp, 표준 MM 알고리즘, "GDExtension — no engine core changes required"). 요구사항: 루트모션 있는 애니 + 발 높이의 루트 본. **[DR 3-0]**
- Bevy에도 `voxell-tech/bevy_motion_matching`가 있어 "엔진 플러그인 계층에서 MM 구현 가능"을 교차 확인. **[DR 3-0]**

**로컬 실측 — 훅 실재 확인 + 중요한 뉘앙스:**
- `AnimationNodeExtension : AnimationNode` (`scene/animation/animation_node_extension.h:35`), 필수 가상 `GDVIRTUAL2R_REQUIRED(PackedFloat32Array, _process_animation_node, PackedFloat64Array, bool)` (line **47**), 공개 API 확정(`doc/classes/AnimationNodeExtension.xml` 존재). 등록: `register_scene_types.cpp:603`.
- ⚠️ **뉘앙스(실측으로 드러난 설계 제약):** `_process_animation_node`는 **타이밍(NodeTimeInfo)만 반환하고 임의 포즈를 반환하지 못한다.** Godot 블렌드 파이프라인은 노드 간에 **포즈가 아니라 가중치+타이밍만** 주고받으며, 최종 포즈는 `_blend_process`→`_blend_apply`가 내부에서 누적해 `Skeleton3D::set_bone_pose_*`로 직접 기록한다(`animation_mixer.cpp:1943-1949`). 즉 커스텀 노드는 **기존 `Animation` 리소스를 가중**할 수만 있다.
- ⟹ **두 가지 통합 경로:**
  1. **`AnimationNodeExtension` 방식** — MM이 "다음에 재생할 클립+시각"을 고르고 기존 애니를 가중 재생(godot-motion-matching이 취하는 경로). 트리 안에 자연 통합되지만 포즈 자체를 합성하진 못함.
  2. **`SkeletonModifier3D` 방식** — MM이 DB에서 포즈를 직접 샘플링·블렌딩해 매 프레임 본 포즈를 기록. 트리 밖에서 스켈레톤을 직접 구동(§4 인러셜라이제이션과 자연 결합). `_process_modification_with_delta`로 델타+본 read/write 완전 확보(`scene/3d/skeleton_modifier_3d.h:111`, 구동 `skeleton_3d.cpp:1177`).

**성숙도 & 구현 실체(딥리서치 확정):**
- `godot-motion-matching`은 **클래식 데이터 주도 MM**(신경망/ONNX 전무 — 리포 grep으로 확인), `src/math/spring.hpp`에 Holden 스프링-댐퍼/인러셜라이제이션 함수 포함, KD-tree(`kd_tree`)·`mm_query`·`mm_feature`로 AnimationTree 완전 통합. `orangeduck/Motion-Matching`(저자 Daniel Holden)이 클래식+LMM 양쪽 캐노니컬 C++ 참조. **[DR 3-0]**
- ⚠️ **"프로덕션 가능"은 추론** — 커뮤니티 오픈소스(Godot 4.4)이지 시판 타이틀 검증 시스템 아님. 프로덕션 하드닝(다수 동시 캐릭터 CPU/메모리 비용, 발 슬라이딩, 이벤트 동기화)은 별도 작업.

**LMM(신경망)을 Godot에 이식 가능한가 — 🟡 가능하나 자체 구축 (실측+딥리서치):**
- 세 네트워크가 **512유닛 소형 MLP**라 플러그인 임베드에 충분히 작음. **[DR 3-0]**
- ❌ 단, **로컬 실측 결과 Godot 트리에 ML/텐서/BLAS 인프라 전무** — onnx·ncnn·tensorflow·libtorch·ggml·Eigen(범용) 없음, `core/math`엔 고정 2×2/3×3/4×4 트랜스폼만(범용 Tensor 타입 없음). 가중치는 `PackedFloat32Array`에 담아야 함.
- ⟹ 경로: **(a) 손으로 짠 C++ MLP 커널**(matmul + ELU/ReLU, 512유닛층 — 논문도 커스텀 추론 라이브러리 사용) 또는 **(b) ONNX Runtime을 GDExtension에 링크** 또는 **(c) 컴퓨트 셰이더**. pau1o-hs가 PyTorch→ONNX→Unity Barracuda로 이미 시연 → Godot GDExtension에서도 전적으로 실현 가능. 단 **논문 타이밍은 커스텀 최적화 추론 라이브러리 의존**이라 나이브 구현은 더 느릴 수 있음.

---

## 2. 컨트롤 릭 / 절차적 런타임 리깅 — ✅ 상당 부분 이미 빌트인 (초판 정정)

> **정정.** 갭 문서 §6은 "컨트롤 릭 / 런타임 리타게팅 없음/제한적"이라 했으나, 4.8-dev 실측 결과 **UE Control Rig / Unity Animation Rigging에 대응하는 IK·제약 노드 스위트가 이미 코어에 들어와 있다.**

### 2.1 Godot의 `SkeletonModifier3D` 아키텍처 (실측)

- 모든 절차적 리깅은 `SkeletonModifier3D`(Node3D 파생) 서브클래스로 표현되며, `Skeleton3D::_process_modifiers()`가 매 프레임 스택 순회하며 `process_modification(delta)` 호출 → influence 블렌딩 → `force_update_all_dirty_bones()` (`scene/3d/skeleton_3d.cpp:1177-1211`).
- 타이밍 모드 `PHYSICS/IDLE/MANUAL` (`skeleton_3d.h:79-82`).
- **커스텀 모디파이어 = 순수 GDExtension 가능**: `GDVIRTUAL1(_process_modification_with_delta, double)` (`skeleton_modifier_3d.h:111`, 바인딩 `.cpp:171`) + `_skeleton_changed`/`_validate_bone_names` 훅. `has_process()`로 외부 애니 없이도 구동 요청 가능(스프링/지글). → **UE Control Rig가 하는 "포즈 후처리 절차 그래프"를 확장으로 구현하는 무(無)코어 경로.**

### 2.2 이미 존재하는 IK/제약 스위트 (실측 — 릴리즈 4.x보다 훨씬 큼)

**IK 계열** (루트 `IKModifier3D`, `ik_modifier_3d.h:35`):
| 클래스 | 파일 | 알고리즘 |
|--------|------|----------|
| `TwoBoneIK3D` | `two_bone_ik_3d.h:35` | 해석적 2본/사지 솔버 (UE Two Bone IK 등가) |
| `CCDIK3D` | `ccd_ik_3d.h:35` | Cyclic Coordinate Descent |
| `FABRIK3D` | `fabr_ik_3d.h:35` | Forward And Backward Reaching IK |
| `JacobianIK3D` | `jacobian_ik_3d.h:35` | 자코비안 기반 |
| `SplineIK3D` | `spline_ik_3d.h:37` | 스플라인 피팅 체인 |

**제약 계열** (루트 `BoneConstraint3D`, `bone_constraint_3d.h:35`): `AimModifier3D`(`aim_modifier_3d.h:35`), `CopyTransformModifier3D`(`copy_transform_modifier_3d.h:35`), `ConvertTransformModifier3D`(`convert_transform_modifier_3d.h:35`).

**직접 모디파이어:** `LookAtModifier3D`(`look_at_modifier_3d.h:36`), `SpringBoneSimulator3D`(`spring_bone_simulator_3d.h:44`, 캡슐/평면/구 콜리전 지원), `BoneTwistDisperser3D`(트위스트 분산), `LimitAngularVelocityModifier3D`, `ModifierBoneTarget3D`. 물리: `PhysicalBoneSimulator3D`(`scene/3d/physics/`, 래그돌). `BoneAttachment3D`(`bone_attachment_3d.h:35`)로 본에 노드 부착(외부 스켈레톤 지원).

> `SkeletonIK3D`(`skeleton_ik_3d.h:115`)는 **deprecated**(단일 체인 FABRIK, `doc/classes/SkeletonIK3D.xml`에 `deprecated`), `SkeletonModifier3D.influence`로 대체. 신규 코드는 위 IK 스위트를 사용.

> Unity Animation Rigging의 Two Bone IK(Root/Mid/Tip이 Target에 도달)가 Godot `TwoBoneIK3D`와 직접 대응. **[DR 3-0, 단 medium — Unity측만 1차출처, Godot IK 커버리지는 위 로컬 실측으로 보강]**

### 2.3 결론

**"컨트롤 릭 부재"는 4.8-dev 기준 부정확하다.** 절차적 IK/에임/카피/스프링본은 빌트인이고, GUI 노드 그래프 저작 UX(UE Control Rig 에디터)만 없다 — 이는 §7(VFX/시네마틱 저작 UX)와 같은 "GUI 저작툴 부재"이지 런타임 능력 부재가 아니다. 특수 릭 로직은 `SkeletonModifier3D` 확장으로 무코어 추가.

---

## 3. 런타임 리타게팅 — 🟡 부분 존재, 완전 패리티는 구조적 격차 (초판 정정)

> **정정.** §6은 "리타게팅은 import-time only/제한적"이라 했으나 **런타임 리타게팅 노드 `RetargetModifier3D`가 실재한다.** 다만 UE/Unity 수준의 "하나의 애니셋을 임의 구조 스켈레톤에 런타임 공유"까지는 트랙-경로 바인딩 때문에 완전하지 않다.

### 3.1 UE/Unity가 하는 것 (검증됨)

- **UE**: `Retarget Pose From Mesh` 애니그래프 노드를 **타깃 캐릭터의 Animation Blueprint에** 배치 → 소스 Skeletal Mesh Component의 포즈를 읽어 `IK Retargeter Asset`으로 리타게팅해 출력. **임포트 베이크가 아니라 평가되는 그래프 노드**이며 Live Link 세션에서도 동작. "새 Animation Sequence 생성 없이 다른 캐릭터로 리타게팅". (오프라인 베이크 대비 런타임 비용 있음.)
- **Unity**: **Avatar** 추상으로 공유 Animator Controller를 상이한 본 구조에 런타임 매핑. `AvatarBuilder.BuildHumanAvatar`로 런타임 아바타 생성까지 가능. "동일 Animator Controller를 여러 모델이 참조".

> Epic/Unity 공식 문서 verbatim. **[DR 3-0]**

### 3.2 Godot의 상한선 (검증됨 + 실측)

- **한계(딥리서치)**: Godot 애니 트랜스폼 트랙은 **스켈레톤 계층에 경로/이름으로 바인딩** → 한 스켈레톤용 애니를 다른 구조 스켈레톤에 재부모/재사용하려면 동일 자식 구조·명명 필요(**본 이름 일치만으로도 불충분 — Bone Rest도 일치해야**). 유일한 정공법은 **import-time 리타게팅**(BoneMap/SkeletonProfile). proposal **#2619** verbatim: "track paths cannot be reparented". **[DR 3-0]**
- **실측 정정 — 런타임 노드는 존재한다**: `RetargetModifier3D`(`scene/3d/retarget_modifier_3d.h:37`)가 `Ref<SkeletonProfile> profile`(line 48) + `use_global_pose`(line 50)를 들고 `_process_modification` 오버라이드(line 99)로 **매 프레임 프로파일 본 이름 매칭으로 포즈 복사**. 임포트 옵션 `retarget/rest_fixer/retarget_method`의 **"Use Retarget Modifier"**(`post_import_plugin_skeleton_rest_fixer.cpp:47`)가 베이크 대신 이 런타임 노드를 주입.
- ⟹ **정확한 그림:** Godot는 *프로파일 기반 본 이름 매칭* 런타임 리타게팅은 되지만, UE IK Rig처럼 *구조가 크게 다른 스켈레톤 간 체인 매핑+IK 보정*까지는 미달.

### 3.3 정밀 실측 — 무엇이 순수 GDExtension이고 무엇이 코어 잠금인가 (확정)

`RetargetModifier3D`를 전문 정독한 결과 경계가 명확해졌다:

- 📋 **`RetargetModifier3D`의 실체 = 구조 리매핑이 아니라 rest-포즈 차분 전송.** 본 매칭은 **오직 이름**(`profile->get_bone_names()` → 각 스켈레톤 `find_bone`, `retarget_modifier_3d.cpp:80-83,102,111`), 소스=부모 스켈레톤·타깃=자식 `Skeleton3D`들 토폴로지(`:181-190`). rest 차분 basis 수학(`cache_bone_rests` `:126-163`)으로 같은 논리 본 집합(리네임됨)을 전송하는 것이지 본 수/계층이 다른 진짜 리매핑이 아니다.
- 🔴 **코어 잠금 지점 = AnimationMixer의 트랙→본 바인딩.** `_update_caches()`가 트랙 NodePath를 `get_node_and_resource`로 노드 해석(`animation_mixer.cpp:733`) 후 `sk->find_bone(path.get_subname(0))`로 **본 이름→인덱스 확정**(`:817,820`), 적용 시 그 `bone_idx`에 기록(`:1943`). **리맵 주입 가상훅 없음** → GDExtension이 "트랙 i→다른 스켈레톤 본 j" 임의 테이블을 넣지 못함. Godot 자신이 쓰는 두 우회(본 리네임 / 트랙 경로 재작성)만 가능.
- ✅ **그러나 포즈 리매핑 엔진 자체는 순수 GDExtension.** 필요한 read/write/rest API 전부 바인딩됨: `find_bone`(`skeleton_3d.cpp:1237`), `get_bone_parent`(`:1248`), `get_bone_rest`/`get_bone_global_rest`(`:1260-1262`), `get_bone_global_pose`/`set_bone_global_pose`(`:1287-1288`), `set_bone_pose_*`(`:1273-1275`). 다른 스켈레톤의 포즈를 읽는 데 제약 없음(public const getter). ⟹ **UE "Retarget Pose From Mesh"의 런타임 포즈-전송 단계 전체가 GDExtension이며, `RetargetModifier3D`를 능가 가능**(본 이름 일치 불요 — 확장 안에서 임의 소스→타깃 인덱스 맵 구동).

### 3.4 경로 (난이도순)

- 🟢 (a) 휴머노이드는 `SkeletonProfileHumanoid` + `RetargetModifier3D`로 이름 매칭 리타게팅 — 지금 가능.
- 🟡 (b) 비표준/이종 스켈레톤은 **`SkeletonModifier3D` 확장**으로: AnimationMixer는 이름 일치하는 **프록시 스켈레톤 하나**만 구동 → 확장 모디파이어가 매 프레임 그 프록시의 글로벌 포즈를 읽어 임의 구조 타깃들에 리타게팅. **코어 불요**(RetargetModifier3D와 동일 패턴, 단 커스텀 매핑). IK 보정 품질은 자체 책임.
- 🔴 (c) 트랙-경로 독립적 "애니 = 리타게팅 가능 자산" 아키텍처(AnimationMixer가 이종 스켈레톤을 직접 구동 = UE IK Rig 완전 패리티)만 **코어 개조** — 트랙→본 리졸버에 리맵 훅 추가.

---

## 4. 고급 블렌딩 · 레이어드 애니 · 인러셜라이제이션 — ⚠️ 진짜 격차

### 4.1 Godot 현행 블렌딩의 실체 (실측)

- **블렌드 파이프라인은 고정 6단계 가중 누적**: `_blend_init → _blend_pre_process → _blend_capture → _blend_calc_total_weight → _blend_process → _blend_apply → _blend_post_process` (`animation_mixer.cpp:1026-1039`). 노드 간에 **포즈가 아니라 가중치+`NodeTimeInfo`만** 전달 — 노드 사이 지속 포즈 버퍼 없음.
- **블렌드 수학**: 위치는 rest/init 기준 **애디티브 누적** `t->loc += (loc - init_loc) * blend` (`animation_mixer.cpp:1431`), 회전은 `interpolate_via_rest`(line 1519). `deterministic=false`면 `total_weight`로 정규화(1284-1288).
- **블렌드 트리 노드**: `Blend2`/`Blend3`/`Add2`/`Add3`/`Sub2`/`OneShot`(MixMode BLEND|ADD)/`TimeScale`/`TimeSeek`/`Transition`. Add2/Sub2로 **애디티브 자체는 가능**(`animation_blend_tree.cpp:837-1013`). `BlendSpace2D`는 Delaunay 삼각분할 + 바리센트릭(`animation_blend_space_2d.cpp:507,549-592`), MAX_BLEND_POINTS=64.

### 4.2 스테이트머신의 한계 (실측)

`AnimationNodeStateMachineTransition`(`animation_node_state_machine.h:37-102`): `SwitchMode IMMEDIATE/SYNC/AT_END`, `AdvanceMode DISABLED/ENABLED/AUTO`, `xfade_time`/`xfade_curve`/`advance_condition`/`advance_expression`/`priority`/`break_loop_at_end`/`reset`. 전이 로직(`animation_node_state_machine.cpp:845-927`): `fade_blend = MIN(1, fading_pos/fading_time)`, 옵션 커브 샘플 — **정확히 2개 상태만 블렌드**(`current` + 단일 `fading_from`, 구조체 `.h:275-278`). Travel은 A* 경로탐색.

**모던 게임 스테이트머신 대비 부재(실측):**
- (a) **레이어드/애디티브 스테이트머신 없음** — 동시 1블렌드(2상태)만. NESTED/GROUPED는 병렬 레이어가 아니라 단일 playback 공유(`StateMachineType`, `.h:113-117`).
- (b) **전이 시 본마스크/아바타마스크 블렌딩 없음**(필터 메커니즘 뿐).
- (c) **모션매칭/포즈서치 훅 없음** — 전이는 수작업 그래프 엣지만.
- (d) **인러셜라이제이션 없음** — 크로스페이드는 선형(또는 커브샘플) 가중 램프.
- (e) **다중 전이 인터럽션 블렌딩 없음** — 단일 `fading_from`만 유지.

> Unity Animation Layers(가중 + 아바타 마스크 + Override/Additive) 대비 Godot는 애니 트리 필터로 부분 대체하나 스테이트머신 레벨 레이어는 없음. **[DR 참조: Unity 6000.4 AnimationLayers, Bevy AnimationGraph Blend/Add 노드 3-0]**

### 4.3 인러셜라이제이션 — 최우선 격차이자 성능 열쇠 (검증됨)

**Inertialization**(Bollo, GDC 2018, *Gears of War*)은 크로스페이드의 현대적 대체다: 전이를 블렌드하는 대신 **포즈 불연속을 포스트프로세스로 흡수**한다. 결정적 성능 이점:
- **크로스페이드**: 전이 중 **소스+타깃 둘 다 평가** → 가변/~2배 프레임 비용.
- **인러셜라이제이션**: 전이 중 **타깃만 평가**(고정 프레임 비용). ⟹ **대량 캐릭터에서 결정적.** UE 모션매칭도 내부적으로 인러셜라이제이션으로 블렌드.

전이 커브는 **5차 최소저크 다항식**(Flash & Hogan 1985):
`x(t) = A·t⁵ + B·t⁴ + C·t³ + (a0/2)·t² + v0·t + x0`
- `v0` = 직전 두 프레임의 유한차분 속도
- `a0` = 착지 시 `x1=v1=a1=0`(t1에서 저크 0)이 되도록 선택: `a0 = (-8·v0·t1 - 20·x0)/t1²`

> Bollo GDC 2018 PDF verbatim: "Inertialization: Only evaluate Target during transition / Fixed anim frame cost" vs "Blending: Evaluate both... Variable anim frame cost". 5차식·a0 라인별 검증. Holden 2023 "Dead Blending"으로 확장. **[DR 3-0]** — **이것이 Godot의 진짜 격차**(Godot는 크로스페이드).

### 4.4 Godot 구현 가능성 (정밀 실측으로 확정)

- 🟢 **인러셜라이제이션 = 순수 GDExtension, 모디파이어 경로가 정답(확정).** 전이 순간 각 본의 (포즈 델타, 속도)를 캡처하고 이후 프레임마다 5차 감쇠 오프셋을 더하는 **`SkeletonModifier3D` 후처리 모디파이어**로 구현 — 스켈레톤 스택 **최후단**에 배치.
  - ✅ **순서 확정**: 모디파이어는 믹서가 포즈를 쓴 **뒤** 실행된다. 믹서 기록(`animation_mixer.cpp:1943-1949`) → `NOTIFICATION_UPDATE_SKELETON`에서 `force_update_all_dirty_bones()` 후 `_process_modifiers()`(`skeleton_3d.cpp:314-341`) → 자식 순서상 **마지막 모디파이어가 최종 포즈를 관찰**(`_find_modifiers` `:1163-1175`, 구동 `:1177-1215`). `_process_modification_with_delta`로 델타 확보(속도 유한차분·오프셋 감쇠용), per-bone read/write 완비.
  - ❌ **`AnimationNodeExtension`로는 불가(확정)**: 6-float 타이밍(NodeTimeInfo)만 반환, 본 인덱스/쿼터니언 인터페이스 없음(`animation_node_extension.cpp:61-71`). §4.1의 "노드는 포즈를 못 주고받음"이 여기서 결론을 확정 — **모디파이어가 유일한 정답.**
  - ⚠️ **엔진이 안 주는 유일한 것 = 전이 신호.** 최후단 모디파이어는 포즈 스트림만 볼 뿐 "방금 전이했다"는 통지를 못 받는다. → **게임 스크립트가 전이 순간을 모디파이어에 직접 주입**(권장, 게임은 자기가 언제 전이하는지 앎) 또는 포즈 불연속 휴리스틱. 이 둘 다 GDExtension 범위 내.
  - 📋 **기존 `capture()`/`UPDATE_CAPTURE`는 인러셜라이제이션이 아니다**: `animation_mixer.cpp:2355-2398`의 `capture()`는 `UPDATE_CAPTURE` **값 트랙**만 대상으로 현재 값을 스냅샷해 `Tween` 이징 스칼라 가중치로 크로스페이드하는 헬퍼(`blend_capture` `:1140-1171`). **속도 캡처 없음, per-bone 5차 아님** — 인접하나 별개. 트리 전역 `inertial` 코드는 UI 스크롤 관성뿐(무관).
- 🟡 **레이어드/본마스크 스테이트머신** = 커스텀 `AnimationNodeExtension` 노드 여러 개를 필터와 조합하거나, 스켈레톤 스택에서 레이어별 모디파이어로 합성 — GDExtension 가능하나 저작 편의는 자체 도구 필요.

---

## 5. 군중 / 대규모 캐릭터 스키닝 — 🔴 인프라 부재, 오프라인 VAT 우회 (전용 딥리서치로 소싱)

> 2차 딥리서치 하니스(102 에이전트, 5각도, 군중·MM 프로덕션 비용 전담)로 이 절을 소싱 근거로 확정. 핵심 주장 전부 3-0 confirmed.

### 5.1 Godot 스키닝의 실체 (실측)

- **GPU 컴퓨트 스키닝 전용**: 전용 컴퓨트 셰이더 `servers/rendering/renderer_rd/shaders/skeleton.glsl`(파이프라인 `mesh_storage.cpp:165-185`). 스키닝+블렌드셰이프가 **컴퓨트 패스**로 실행(정점셰이더 인라인 아님, CPU 아님). **CPU 소프트 스키닝 폴백 없음**(3.x 폴백은 이 트리에 부재).
- 정점당 최대 **8본 영향**(`ARRAY_FLAG_USE_8_BONE_WEIGHTS`, `mesh.h:158`; 셰이더 4+4 그룹), 아니면 4본. **`MAX_BONES` 컴파일 상수 없음** — 본 수는 동적 SSBO(`bone_transforms`).
- 블렌드셰이프도 동일 컴퓨트 패스(정규화 모드 `BLEND_SHAPE_MODE_NORMALIZED`).

### 5.2 군중 격차 (실측)

- ❌ **애니메이션 LOD 없음** — 거리별 스켈레톤 갱신율/비용 축소 시스템 부재. 유일한 거리 처리는 `VisualInstance3D` visibility range(`visual_instance_3d.h:128-170`)로 **인스턴스 통째 컬/페이드**일 뿐 애니 비용을 줄이지 않음.
- ❌ **MultiMesh는 스켈레톤/본 미지원** — `multimesh_*` API(`rendering_server.h:257-286`)에 본 파라미터 없음. 인스턴스당 트랜스폼/컬러/커스텀데이터만. **수천 개 독립 스켈레탈 캐릭터를 MultiMesh로 스키닝하는 빌트인 경로 없음.**
- ⟹ 애니 캐릭터마다 **독자 `Skeleton3D` + 스킨 `MeshInstance3D`**(각각 컴퓨트 스키닝 디스패치) 필요. 실질 스케일 한계 = N개 컴퓨트 디스패치 + N개 모디파이어 처리, visibility range로만 완화.

### 5.3 업계가 실제로 하는 것 — 스키닝을 CPU·스켈레톤에서 떼어낸다 (검증됨)

AAA는 스켈레탈 애니를 "더 빠르게" 하는 게 아니라 **작업 자체를 CPU와 스켈레톤에서 제거**한다:

- **VAT(Vertex Animation Texture) / 베이크투텍스처**: 본 행렬(또는 정점별 포즈)을 **프레임마다 텍스처에 구워** 정점셰이더가 시간 파라미터로 샘플링 → **런타임 스켈레톤/스키닝 계산이 아예 없음**. GPU 하드웨어 인스턴싱과 결합. **[DR 3-0]** (SideFX VAT, dawnarc/VertexAnimSample, NVIDIA GPU Gems 3 Ch.2 "skinned instancing", GPU Instancer)
- **스케일 실측치**: NVIDIA GPU Gems 3이 **2007년 하드웨어**(Core 2 Duo 2.93GHz + 8800 GTX @1280×1024)에서 **독립 포즈 캐릭터 9,547개를 ~34fps**로 렌더 — 본 행렬을 애니 텍스처에 넣고 DX10 인스턴싱. 현대 HW는 훨씬 높음. **[DR 3-0]**
- ⚠️ **VAT의 결정적 한계**: **런타임 블렌딩·IK 불가, 고정 클립만.** 이것이 하이브리드 분리의 근본 원인. **[DR 3-0]**

### 5.4 하이브리드 + LOD — 보편 프로덕션 모델 (검증됨)

- **하이브리드**: 히어로 = 풀 스켈레탈 릭(+IK+블렌딩), 원거리 군중 = 베이크텍스처 인스턴스 메시. UE **MassCrowd**가 정확히 이 구조 — 근거리=스폰된 액터(스켈레탈 HighRes/LowRes), 중/원거리=StaticMesh/ISM/HISM(베이크 정점 애니), 최원거리=컬. 문서가 ISM을 "액터 표현의 최저비용"이라 명시. **[DR 3-0]**
- **애니메이션 LOD (틱레이트 스로틀)**: UE **URO(Update Rate Optimization)** — 원거리 캐릭터 애니 틱 빈도를 낮춤(Epic 권장 **원거리 ≤15Hz + 보간 비활성**). MassEntity/MassCrowd가 병렬 LOD-collector·visualization-LOD 프로세서로 전용 군중 LOD 운용. **[DR 3-0]**
- **Godot 대응**: 위 전부가 **엔진 코어가 아니라 플러그인/에셋 파이프라인 계층** 능력이다(베이크+커스텀 셰이더). 단 **직접 손으로 구축**해야 함:
  - VAT 베이커: 에디터 스크립트/GDExtension으로 애니를 텍스처에 굽고 커스텀 정점셰이더 + `MultiMeshInstance3D`로 재생(스켈레톤 스키닝 우회 → 코어 불요).
  - 애니 LOD 매니저: `PROCESS_MANUAL` 콜백 모드(`animation_mixer.h`) + 거리 기반 `advance()` 스로틀을 스크립트로 구현(URO 등가).

### 5.5 결론 & 남은 불확실성

군중은 **엔진 빌트인 해법이 없다.** AAA 오픈월드 기준으로 L1 근거리 군중(20~100)은 런타임 블렌딩이 필요하므로 VAT로 충분하지 않다. 현실적 경로:
- **L0 영웅/NPC** = 풀 스켈레탈(MM+IK+인러셜라이제이션), 기존 `Skeleton3D` + GDExtension.
- **L1 근거리 군중** = 스켈레탈 + URO식 틱 스로틀. `Skeleton3D` × N개가 감당 가능한지 벤치마크 필요.
- **L2 중거리 군중** = VAT + `MultiMeshInstance3D` + 커스텀 셰이더.
- **L3 원거리 군중** = 컬링.
- **MultiMesh 스켈레톤 지원**(코어)은 man-year급. AAA 군중 품질을 위해 P2 검토 대상.

> **소싱 한계(딥리서치 caveat):** ① NVIDIA 9,547 벤치는 2007 HW — 기법 확장성은 증명하나 절대수치는 역사적. ② **원신/붕괴 스타레일 등 모바일 가챠 오픈월드의 실제 캐릭터 수·LOD 예산은 miHoYo/GDC 1차 기술발표를 찾지 못해 미검증** — 이 부분은 추정으로 취급. ③ Godot 네이티브 VAT 군중 파이프라인은 **기성 애셋이 아니라 직접 구축** 대상(현존 시판 도구 확인 안 됨).

---

## 6. GDExtension vs 코어 경계 (종합)

| 항목 | 순수 GDExtension | 소규모 코어 | 대규모 코어/오프라인 |
|------|:---:|:---:|:---:|
| 모션 매칭(표준) | ✅ (`AnimationNodeExtension`/`SkeletonModifier3D`, 애드온 실재) | | |
| Learned MM(신경망 추론) | ✅ (512유닛 MLP 소형 — 자체 커널 or ONNX 링크; 코어 불요) | | |
| 커스텀 IK/컨트롤 릭 로직 | ✅ (`SkeletonModifier3D`) | | |
| 인러셜라이제이션 | ✅ (후처리 모디파이어 — 순서·API 확정) | | |
| 레이어드/본마스크 SM | 🟡 (커스텀 노드+모디파이어 조합) | | |
| 프로파일 이름매칭 런타임 리타게팅 | ✅ (`RetargetModifier3D` 빌트인) | | |
| 이종 스켈레톤 런타임 포즈-리매핑 엔진 | ✅ (`SkeletonModifier3D`로 프록시→타깃, 코어 불요) | | |
| AnimationMixer가 이종 스켈레톤 직접 구동(IK-Rig 완전 패리티) | | 🟡 (트랙→본 리졸버 리맵 훅) | ✅ |
| 벌크 본-포즈 API(고빈도 대형 스켈레톤) | | ✅ (현재 per-bone만) | |
| VAT 군중(오프라인 베이크 + 커스텀 셰이더) | ✅ (베이커+`MultiMeshInstance3D`) | | |
| 애니 LOD(URO 등가, 틱 스로틀) | ✅ (`PROCESS_MANUAL`+거리 `advance()`) | | |
| MultiMesh 스켈레톤 지원 / 네이티브 애니 인스턴싱 | | | ✅ |

> **유일한 "소규모 코어" 후보:** 대형 스켈레톤 고빈도 쓰기용 **벌크 본-포즈 API 부재**(현재 `set_bone_pose_*` per-bone만, 벌크 `set_bone_poses()` 없음 — `skeleton_3d.h:273-286` 실측). MM/인러셜라이제이션을 네이티브 `SkeletonModifier3D` 안에서 돌리면 VM 경계는 없어 대개 충분하나, 극한 군중에선 패치가 도움.

---

## 7. 현실적 대안 & 전략 결론

### 7.1 우선순위 로드맵 (난이도·ROI 순, AAA 기준 2026-08-18 개정)

1. 🟢 **모션 매칭 도입** — `godot-motion-matching` 애드온을 시작점으로 평가·포크. 히어로 캐릭터 로코모션 품질을 즉시 끌어올림. 순수 GDExtension, 코어 불요.
2. 🟢 **인러셜라이제이션 모디파이어 자체 구현** — 5차 감쇠 `SkeletonModifier3D`. 모든 전이 품질↑ + 대량 캐릭터 전이 비용↓. **ROI 최고, 위험 최저.**
3. 🟢 **빌트인 IK 스위트 활용** — `TwoBoneIK3D`/`LookAtModifier3D`/`SpringBoneSimulator3D`로 발 IK·조준·2차 모션. 특수 릭은 `SkeletonModifier3D` 확장.
4. 🟢 **레이어드/본마스크 애니** — 상체(전투)/하체(로코모션) 분리 등은 애니 트리 필터 + 커스텀 노드로. AAA 필수 요구. GDExtension으로 실현 가능.
5. 🟡 **런타임 리타게팅** — 휴머노이드는 `RetargetModifier3D`. 이종 스켈레톤 공유가 필요하면 프록시 스켈레톤 + `SkeletonModifier3D` 확장 매핑.
6. 🟡 **페이셜 애니메이션 (신규)** — FACS 블렌드셰이프(ARKit 52개) + 오디오 구동 립싱크. Godot 블렌드셰이프 트랙은 존재하나, FACS 표준 릭·립싱크 솔버·페이셜 캡처 파이프라인은 자체 구축. GDExtension으로 가능하나 물량 큼.
7. 🟡 **물리 기반 애니메이션 블렌딩 (신규)** — 래그돌 전이, 키프레임↔물리 블렌드. `PhysicalBoneSimulator3D` + `SkeletonModifier3D`로 구현 가능.
8. 🟡 **클로스/헤어 시뮬 (신규)** — `SpringBoneSimulator3D` 기반 확장. AAA 캐릭터 퀄리티. GDExtension으로 가능하나 물량 큼.
9. 🔴 **군중** — L0/L1/L2/L3 하이브리드(LOD: 근거리=스켈레탈+URO, 중거리=VAT, 원거리=컬). MultiMesh 스켈레톤 지원(코어)은 man-year급, P2 검토.
10. 🔴 **애니메이션 압축·스트리밍 (신규)** — ACL/ozz급 압축 + 디스크→메모리 스트리밍. VT 인프라에 의존(#7144). man-year급, P2(VT 이후).

### 7.2 AAA 관점 (2026-08-18 개정)

- 원신 하한은 폐기. AAA(Horizon/Cyberpunk 2077/UE5.4+)가 목표. 모션매칭·인러셜라이제이션·IK는 **순수 GDExtension으로 실물 MVP가 가능**해 "비싸서 못 함"이 아니다.
- **진짜 물량 병목은 다섯 곳:** (a) 군중 스키닝 인프라(MultiMesh 스켈레톤, man-year급 코어), (b) 애니메이션 압축·스트리밍(man-year급, VT 의존), (c) 페이셜 애니메이션(FACS·립싱크, 물량 큼), (d) 클로스/헤어 시뮬(물량 큼), (e) AAA 저작 UX(모션매칭 DB 튜닝·본마스크 편집 GUI) — 후자는 §7(VFX/시네마틱)와 동일 성격.
- **AAA 목표엔 부분 충분, 군중·압축·페이셜은 추가 투자 필요.** Godot의 빌트인 IK + 자체 모션매칭/인러셜라이제이션 + 레이어드 애니 + 하이브리드 군중(VAT+URO) + 자체 FACS 릭이면 아트 디렉션으로 상당 부분 도달 가능하나, MultiMesh 스켈레톤과 애니메이션 스트리밍은 코어 작업이 불가피하다.

---

## 부록 A. 딥리서치 검증 통계 (2회 하니스)

- **1차(핵심 4격차)**: 6각도 → 30 소스 → 130 주장 → **25 주장 3표 검증 → 25 confirmed / 0 refuted** → 11 findings.
- **2차(군중·MM 프로덕션 비용)**: 5각도 → 30 소스 → 상위 주장 3표 검증 → 핵심 전부 3-0 confirmed.
- 1차 출처: Clavet GDC2016, Holden et al. LMM(SIGGRAPH2020) + Table1, Bollo GDC2018(inertialization), Ubisoft La Forge LMM, Epic UE Motion Matching/Runtime IK Retargeting/MassCrowd/URO 문서, Unity 6000.4 Retargeting/AnimationLayers/Animation Rigging, NVIDIA GPU Gems 3 Ch.2, SideFX VAT, Godot proposals #11123·#2619·#6122·#6816 / PR #99181 / `design-of-the-skeleton-modifier-3d`, `GuilhermeGSousa/godot-motion-matching`, `orangeduck/Motion-Matching`, `pau1o-hs/Learned-Motion-Matching`, Bevy `bevy_animation_graph`·`bevy_motion_matching`.

### 부록 A-2. 초판 미해결 질문 → 최종 판정

1. **런타임 리타게팅, 순수 GDExtension 한계?** → **판정: 포즈-리매핑 엔진은 순수 GDExtension**(§3.3). 코어 잠금은 오직 "AnimationMixer가 이종 스켈레톤을 직접 구동"(트랙→본 리졸버 `animation_mixer.cpp:733,817,820,1943`, 리맵 훅 없음). 프록시 스켈레톤 + `SkeletonModifier3D` 우회로 완전 대체.
2. **인러셜라이제이션, `AnimationNodeExtension`-only 가능?** → **판정: 불가. 모디파이어 경로가 정답**(§4.4). 노드는 타이밍만 반환(`animation_node_extension.cpp:61-71`), 모디파이어는 믹서 뒤에 실행(`skeleton_3d.cpp:314-341`) — per-bone 델타 흡수 가능. 엔진 미제공은 전이 신호뿐(스크립트 주입).
3. **`godot-motion-matching` 실전 비용 / LMM 신경망 포팅?** → **판정: 클래식 MM은 스프링 인러셜라이제이션 포함 완성형 GDExtension**(신경망 무). **LMM은 512유닛 소형 MLP 3종이라 GDExtension 이식 가능**(자체 C++ MLP 커널 or ONNX 링크; 트리에 ML/BLAS 인프라는 전무 — 자체 구축, §1.5). 상수 CPU·70배 메모리 절감, 단 추론 계산 추가.
4. **군중 성능 전용 소싱** → **§5 전면 보강 완료**(VAT/GPU 인스턴싱, UE MassCrowd/URO, 하이브리드+LOD). **남은 미검증: 원신/miHoYo 실제 LOD 예산**(1차 기술발표 부재).

### 부록 A-3. 잔여 정량 미해결 (하드 넘버 부재)

- 클래식 MM 최근접이웃 검색의 **캐릭터당 프레임 CPU 비용(µs)** 및 폴백 전 **동시 MM 캐릭터 상한** — 하드 넘버 1차 출처 미확보.
- LMM 3망을 **Godot GDExtension(자체 MLP vs ONNX vs 컴퓨트셰이더)에서 돌릴 때 캐릭터당 실측 추론 비용** — 벤치 필요(논문은 커스텀 최적화 추론 라이브러리 의존).
- 원신/붕괴 스타레일의 캐릭터 애니 수·LOD 예산 (miHoYo GDC 부재).

## 부록 B. 로컬 소스 실측 인덱스 (Godot 4.8-dev, file:line)

- 애니 믹서/블렌드: `scene/animation/animation_mixer.{h,cpp}` (파이프라인 `.cpp:1026-1039`, 블렌드수학 1431/1519, 루트모션 1290, 포즈기록 1943-1949, `_post_process_key_value` `.h:366`).
- 애니 트리/확장: `scene/animation/animation_tree.{h,cpp}`, `animation_node_extension.{h,cpp}`(`.h:47`).
- 스테이트머신: `scene/animation/animation_node_state_machine.{h,cpp}`(전이 `.h:37-102`, 로직 `.cpp:845-927`, 단일 fading_from `.h:275-278`).
- 블렌드 트리/스페이스: `animation_blend_tree.cpp:837-1013`, `animation_blend_space_2d.cpp:507,549-592`.
- Animation 리소스: `scene/resources/animation.{h,cpp}`(트랙타입 `.h:48-58`, 보간 60-66, 압축 349-364, 샘플링 `.cpp:3984-4028`).
- 스켈레톤/모디파이어: `scene/3d/skeleton_3d.{h,cpp}`(`_process_modifiers .cpp:1177`, 본포즈 API `.h:273-300`), `skeleton_modifier_3d.{h,cpp}`(`_process_modification_with_delta .h:111`).
- IK/제약: `ik_modifier_3d.h:35`, `two_bone_ik_3d.h:35`, `ccd_ik_3d.h:35`, `fabr_ik_3d.h:35`, `jacobian_ik_3d.h:35`, `spline_ik_3d.h:37`, `bone_constraint_3d.h:35`, `aim_modifier_3d.h:35`, `look_at_modifier_3d.h:36`, `spring_bone_simulator_3d.h:44`, `skeleton_ik_3d.h:115`(deprecated).
- 리타게팅: `scene/3d/retarget_modifier_3d.{h,cpp}`(이름매칭 `.cpp:80-83,102,111`, rest수학 `cache_bone_rests .cpp:126-163`, 소스=부모/타깃=자식 `:181-190`, 처리 `:376-382`). 트랙→본 바인딩(코어 잠금) `animation_mixer.cpp:733,817,820,1943`. 본 API 바인딩 `skeleton_3d.cpp:1237-1288`. 임포트 `post_import_plugin_skeleton_rest_fixer.cpp:47,483-491,556-567`.
- 인러셜라이제이션 관련: 모디파이어 실행순서 `skeleton_3d.cpp:314-341` + `_process_modifiers 1163-1215`, 믹서 포즈기록 `animation_mixer.cpp:1943-1949`, `capture() 2355-2398`·`blend_capture 1140-1171`(별개). ML/BLAS 라이브러리 트리 전역 부재(onnx·ncnn·libtorch·ggml·Eigen(범용) 없음).
- 스키닝/군중: `servers/rendering/renderer_rd/shaders/skeleton.glsl`, `mesh_storage.cpp:165-185`, `mesh.h:158`, `rendering_server.h:257-304`, `visual_instance_3d.h:128-170`.

---

## 2026-08-18 개정 — AAA 기준 재채점

> **전제 변경:** 포크 + 업스트림 디스커넥트 + Full-Nanite 커밋. "코어 수정 필요"는 탈락 사유가 아님. 목표는 UE5.4+ / Horizon Forbidden West / Cyberpunk 2077 급 AAA. 스타일라이즈드 축소(원신 하한)는 폐기. 판단 축은 "man-year급인가" 하나뿐.

### 뒤집힌 판정 (Delta Table)

| 항목 | 기존 판정 (2026-08-03) | AAA 재판정 (2026-08-18) | 근거 |
|------|------------------------|--------------------------|------|
| **① 인러셜라이제이션** | 🟢 순수 GDExtension, `SkeletonModifier3D` 후처리 | **판정 유지.** ROI 최고, 위험 최저. AAA에선 "있으면 좋은"이 아니라 모션매칭+대량 캐릭터의 결정적 성능 열쇠. 전이 신호 주입만 게임코드가 담당. | Bollo 2018 5차 최소저크. UE도 내부적으로 인러셜라이제이션으로 블렌드. |
| **② 레이어드/본마스크 스테이트머신** | 🟡 GDExtension 커스텀 노드+모디파이어 조합 | **🟡→🟢 AAA 필수 요구로 격상, GDExtension으로 실현 가능.** 상체(전투/사격)와 하체(로코모션)의 동시 애니메이션 레이어는 AAA 캐릭터의 기본. AnimationTree 필터 + 커스텀 `AnimationNodeExtension` + `SkeletonModifier3D`로 구현 가능. 코어 불요. | UE Animation Layers, Unity Animation Layers. GDExtension 조합으로 우회 가능. |
| **③ 런타임 리타게팅** | 🟡 부분 (RetargetModifier3D 존재, 이종 스켈레톤은 프록시 우회) | **🟡→🟡 판정 유지하나 중요도 격상.** AAA에서 "하나의 애니셋을 임의 스켈레톤에 공유"는 NPC·적군 대량 제작의 물량 열쇠. 프록시 스켈레톤 + `SkeletonModifier3D` 확장으로 코어 없이 가능. IK Rig 완전 패리티(코어)는 P2. | UE IK Retargeter. 프록시 패턴이 AAA 물량에도 충분한지 검증 필요. |
| **④ 군중 스키닝** | 🔴 "오프라인 VAT 우회"로 강등 | **🔴→🔴 판정 유지하나 평가 변경 — AAA 오픈월드에선 VAT로 충분하지 않을 수 있음.** VAT는 고정 클립만 재생 → AAA 오픈월드 군중(200~500+ 동시 캐릭터)이 런타임 블렌딩·IK·히트 리액션을 요구하면 VAT로는 부족. **코어 애니 인스턴싱(MultiMesh 스켈레톤)은 man-year급 작업.** 하이브리드(LOD: 근거리=스켈레탈, 중거리=VAT, 원거리=컬)가 현실적 경로이나, AAA 군중 밀도 기대치에선 VAT만으로는 하한 미달. | UE MassCrowd, URO. AAA 오픈월드(Cyberpunk/Witcher 3/Horizon) 군중 밀도. |
| **⑤ 페이셜 애니메이션 (신규)** | 미평가 | **🟡 신규. AAA 필수 격차.** 오디오 구동 립싱크, FACS 블렌드셰이프(ARKit 52개), 메타휴먼급 페이셜 릭. Godot은 블렌드셰이프 트랙(`blend_shape_track_interpolate`)은 존재하나, FACS 표준 릭·립싱크 솔버·페이셜 캡처 파이프라인은 전무. GDExtension으로 구축 가능하나 물량 큼. | UE MetaHuman, Live Link Face. Godot: `animation.cpp:3984-4028`. |
| **⑥ 물리 기반 애니메이션 블렌딩 (신규)** | 미평가 | **🟡 신규. AAA 필수 격차.** 래그돌 전이(ragdoll-to-animation blend), 피직스 애니메이션 블렌딩(Death/Impact). Godot은 `PhysicalBoneSimulator3D`(래그돌)와 `SpringBoneSimulator3D`(2차 모션)는 존재하나, 키프레임↔물리 블렌드 계층은 전무. GDExtension으로 `SkeletonModifier3D`에서 포즈 보간 가능. | UE Physical Animation, Unity Animation Rigging. Godot: `physical_bone_simulator_3d.h`. |
| **⑦ 애니메이션 압축·스트리밍 (신규)** | 미평가 | **🔴 신규. AAA 필수 격차, man-year급.** fire가 이슈 #7144에서 "blocked on the rendering team for several years"라 한 항목. ACL/ozz급 압축, 애니메이션 데이터 스트리밍(디스크→메모리), LOD별 애니 데이터 해상도. Godot은 `Animation::COMPRESSION_*` 플래그(`animation.h:349-364`)만 존재, 런타임 압축·스트리밍 인프라 전무. 코어 신규 서브시스템. | Godot #7144, UE ACL, Unity Playable. |
| **⑧ 클로스/헤어 시뮬과 스켈레톤 연동 (신규)** | 미평가 | **🟡 신규. AAA 요구.** 클로스 시뮬(스커트/망토/머리카락)이 본 포즈에 반응. `SpringBoneSimulator3D`는 2차 모션 기초 수준. 체인 시뮬/자체 충돌/바람장 없음. GDExtension으로 구축 가능하나 물량 큼. | UE Chaos Cloth. Godot: `spring_bone_simulator_3d.h:44`. |

### 군중 스키닝 재평가 (AAA 오픈월드 밀도 기대치)

**기존 판정의 문제:** "오프라인 VAT 우회"는 원신급(군중 밀도 낮음, 제한된 상호작용)에는 타당했으나, AAA 오픈월드 기준으로는 불충분하다.

| 군중 계층 | 캐릭터 수 (AAA 기준) | 요구사항 | Godot VAT 적합성 | Godot 대안 |
|-----------|----------------------|----------|------------------|------------|
| L0 영웅/NPC | 1~20 | 풀 스켈레탈(MM+IK+블렌딩) | ❌ (VAT는 블렌딩 불가) | ✅ 기존 `Skeleton3D` + GDExtension |
| L1 근거리 군중 | 20~100 | 스켈레탈, 제한 블렌딩, 애니 LOD | ❌ (VAT는 블렌딩 불가) | ⚠️ `Skeleton3D` × N개, URO식 틱 스로틀 |
| L2 중거리 군중 | 100~500 | VAT or 스켈레탈 LOD, 걷기/대기만 | ✅ VAT | ✅ `MultiMeshInstance3D` + 커스텀 셰이더 |
| L3 원거리 군중 | 500+ | VAT, 컬링 | ✅ VAT | ✅ `MultiMeshInstance3D` + visibility range |

**결론:** L1 근거리 군중(20~100)이 AAA의 진짜 병목. VAT는 블렌딩이 안 돼 배회·반응·전투 애니가 불가능. `Skeleton3D` × 100개를 URO 틱 스로틀로 감당 가능한지 벤치마크 필요. **MultiMesh 스켈레톤 지원(코어)은 man-year급이지만, AAA 군중 품질을 위해선 P2로 검토해야 한다.**

### 애니메이션 압축·스트리밍 (#7144) 재평가

fire의 이슈 #7144 발언("blocked on the rendering team for several years")은 이 항목이 **순수 애니메이션 문제가 아니라 렌더링 팀의 텍스처/버퍼 스트리밍 인프라에 의존**함을 의미한다. VT 작업(§3)과 연계:
- VT의 디스크→GPU 스트리밍 인프라가 먼저 구축돼야 애니메이션 스트리밍도 그 위에 얹을 수 있음.
- ACL/ozz 압축은 GDExtension으로도 가능하나, Godot `Animation` 리소스와의 통합(런타임 디코드·포즈 재구성)은 코어 작업.
- **man-year급, P2 (VT 이후).**

### 인라인 수정 내역

- **§0 한 장 요약 (L18-19):** "군중 스키닝(수천 캐릭터)" → "🔴 코어 or 오프라인 VAT 우회" 판정은 유지하나, AAA 기준으로 VAT 한계 명시 추가.
- **§5.5 결론 (L250-253):** "오프라인 VAT 파이프라인 + 자체 애니 LOD 매니저가 현실적 정답" → "AAA 근거리 군중(20~100)에는 VAT 불충분. 하이브리드 + URO 틱 스로틀 + MultiMesh 스켈레톤(코어) 검토 필요"로 수정.
- **§7.1 우선순위 로드맵 (L281-286):** 군중 항목을 "오프라인 VAT"에서 "하이브리드 LOD + URO 틱 스로틀 + MultiMesh 스켈레톤(코어/검토)"로 확장.
- **§7.2 스타일라이즈드 축소 관점 (L291-292):** "원신도 히어로 = 정교한 스테이트머신+IK, 군중 = 경량/VAT식" → AAA로 대상 변경. "원신급 목표엔 충분" → "AAA 목표엔 부분 충분, 군중·압축·페이셜은 추가 투자 필요"로 수정.
