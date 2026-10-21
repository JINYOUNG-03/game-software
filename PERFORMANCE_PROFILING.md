# 렌더링 최적화와 프로파일링

## 적용 구조

- Actor/SceneGraph의 소유권과 깊이 순서를 유지한다.
- 매 프레임 한 번 그래프를 순회하고 화면 밖 대상을 정렬 전에 제외한다.
- 패스별 렌더 목록을 재사용하고 월드 위치는 부모 변경 시에만 다시 계산한다.
- Actor 종류별 인덱스로 NPC/적/아이템 조회 시 전체 그래프 검색을 피한다.
- 월드 모델은 GLB에서 변환한 공유 RGBA 아틀라스와 바인딩을 한 번 읽는다. 모델당 인스턴스 1개이며 구형 도형 캐시는 사용하지 않는다. 자세한 원본 매핑은 RESOURCE_INTEGRATION.md 참조.
- 렌더 큐는 최대 8,192개 사각형/마름모/임의 사각형을 순서대로 인스턴싱한다.
  색상, 재질, UV는 인스턴스 속성이다. 재질별 전역 재정렬은 하지 않는다.
- 64개의 GPU 배치 슬롯에서 변하지 않은 데이터를 재사용한다. 변경 시 배치 단위 업로드한다.
  384×384 월드의 지형·환경은 32×32 청크 144개로 묶고 화면 주변 그룹만 탐색한다.
  모든 지형 Actor/충돌은 메모리에 유지한다. 디스크 스트리밍이나 메모리 해제는 아니다.
- 미니맵 지형은 240×225 텍스처에 캐싱한다. 지형 위치/종류/표시/구성 변경 시 갱신한다.
  움직이는 마커는 매 프레임 별도로 그린다. 기본은 64×64 지역 지도이며 8타일씩 이동하는
  창의 원점과 지도 모드도 캐시 키에 포함한다. 전체 지도는 2×2 타일을 묶어 생성 비용을 줄인다.
- 글리프는 GDI의 0~64 커버리지를 0~255로 변환한 R8 아틀라스에 저장한다.
  기본 1,024×1,024, 최대 16페이지. 글리프 실패/페이지 한도는 glyph_failure로 집계한다.
- 후처리는 가로·세로 각각 1/4 해상도에서 밝기 추출과 분리 블러를 수행한 후 합성한다.
  후처리 호출 수는 4회지만 전체 픽셀 연산량을 줄인다.
- 지형은 타일당 1개 인스턴스다. 물결·잔물결·물가/길 가장자리·젖은 흙의 세부 표현은
  카메라 이동에 고정된 월드 좌표 셰이더로 계산한다. 인스턴스 stride는 표면 데이터 포함 84 bytes다.
- 물가/길의 4방향 연결 마스크와 수심은 생성·수문 개방 후에만 계산한다.
  매 프레임 전체 지형을 순회하거나 주변 타일을 조회하지 않는다.
- 주변 창문/등불 조명은 가시 Effect Actor 중 화면 중심에 가까운 최대 8개를 선택해
  기존 배치의 uniform으로 전달한다. 광원별 추가 FBO/드로우 패스는 없다.
  이는 스타일화한 2D 광원이며 노멀맵/PBR/광원별 그림자 구현이 아니다.
- 건물·캐릭터·지형·소품은 resource의 GLB 46개를 변환한 단일 2048×2048 아틀라스(16 MiB)를 공유한다.
  스키닝/자세 샘플링은 오프라인에서 수행하며 캐릭터당 런타임 쿼드 하나다.
  NPC는 외형 18종, 4방향, 정지/8단계 걷기, 플레이어 공격은 12단계다.
  물/불꽃/안개/바람과 간략한 2.5D 그림자는 기존 셰이더/Effect Actor를 유지한다.
  타격은 combat, 자세 갱신은 animation, 칼 잔상 제출은 render_effects에서 확인한다.
  사람의 화면 가로 비율 1.40배와 잔상 비율은 기존 쿼드 좌표에서 적용해 인스턴스/드로우콜을 늘리지 않는다.
  전용 칼 베기 키와 2본 팔 IK는 아틀라스 변환 때만 계산하며 런타임 IK 비용은 없다.
- uniform 위치는 초기화 시 캐싱한다. OpenGL 상태/버퍼 설정은 개별 도형이 아니라 배치 단위다.

## 출력과 보존

기본적으로 실행 중 지속 계측한다. 외부 서버로 전송하지 않는다.

- 파일: 실행 파일 옆 Profiles/profile_<UTC시각>_<PID>_0.jsonl 등.
- 콘솔: 약 1초마다 summary 이벤트. 프레임별 콘솔 출력은 제거했다.
- frame: 매 프레임 CPU 시간, 작업량, 상태.
- gpu_frame: 비동기로 도착하는 GPU 타임스탬프 구간. session_id + frame_id로 연결.
- startup: 초기 생성/모델 로딩 비용. 정상 플레이 FPS와 분리.
- world_state: 월드 시간 2분(실시간 약 2초)마다 NPC 행동·체력·재산·목표 가중치·지식·장부 소유권 표본.
  일시정지 중에는 월드 시간이 멈추며 이 표본도 생성하지 않는다.
  시작 안내 및 H 도움말도 월드를 정지시키며 scene_state.paused에 반영한다.
  3장면 도입과 NPC 대화도 같은 일시정지 상태에 포함하며 로컬 모니터에는 계속 표본을 게시한다.
  대화 생성·진행·선택 처리는 interaction, 대화 텍스트는 render_ui/text로 계측한다.
  상황별 안내 계산은 기존 quest_update로 계측하고, 텍스트·이름표 비용은 text/render_ui에서 확인한다.
- session: Debug/Release, GL 버전, GPU 정보, 스키마.
- log_segment / session_end: 순환 저장 및 종료 상태.
- 실행 세션마다 16 MiB × 4 파일을 순환한다. 같은 세션의 가장 오래된 구간부터 덮어쓴다.
  이전 실행 세션 파일은 자동 삭제하지 않는다.
- 로그는 최대 512개 이벤트 대기열과 별도 저장 스레드를 사용한다.
  250ms 또는 64개 이벤트 단위로 쓰고, 밀리면 렌더 스레드를 기다리게 하지 않고 누락 수를 기록한다.
  파일 쓰기 실패는 log_file_failed, 누락은 log_dropped_total에 남긴다.
- 측정 오버헤드 비교: 실행 전에 환경변수 SIMPLEGAME_PROFILE=0을 지정하면 CPU/GPU 프로파일링과 로그를 끈다.
  렌더링 최적화 자체는 그대로 유지된다.

## 해석 규칙

모든 CPU/GPU 시간은 ms, 크기는 bytes, 누적 이벤트는 count 단위다.
안정적인 영문 snake_case 키를 사용한다. 스키마를 바꾸면 schema_version을 올린다.
현재 스키마는 9이며 분석기는 기존 스키마 1·2·3·4·5·6·7·8 로그도 읽는다.

| 필드 | 의미 |
|---|---|
| frame_interval_ms | 이전 프레임 시작부터 현재 시작까지의 실제 간격. 첫 플레이 프레임은 0으로 제외 |
| cpu_ms.frame | 현재 프레임 시작부터 제출·SwapBuffers 완료까지. 로그 인코딩 이전 |
| cpu_ms.update / world_update / movement / ai | 전체 업데이트, 월드 업데이트, 플레이어 이동, 짐승 AI·이동·공격 |
| cpu_ms.navigation / collision_rebuild | 길찾기 필드 및 충돌 캐시 갱신 |
| cpu_ms.scene_prepare / scene_sort | 가시성 판정·목록 생성 및 정렬 |
| cpu_ms.render_ground / render_shadow / render_world / render_effects / render_ui | 렌더 명령 생성·해당 구간 제출 시간 |
| cpu_ms.batch_submit | GPU 업로드 및 실제 OpenGL 제출에 쓴 CPU 시간 |
| cpu_ms.postprocess / swap_wait | 후처리 제출과 표시 대기. swap_wait만으로 GPU 병목을 확정하지 말 것 |
| cpu_ms.text / glyph_rasterize / minimap_rebuild | 텍스트 생성, 신규 글리프 래스터화, 미니맵 캐시 갱신 |
| cpu_ms.renderer_init / font_init / model_load / model_build / model_save / world_generate | 초기화·생성·캐시 파일 I/O |
| scope_calls | 구간 실행 횟수. 0이면 해당 프레임에 실행되지 않은 구간 |
| scope_draw_calls / scope_upload_bytes | 구간 안에서 실제 제출된 호출 수와 업로드량 |
| gpu_interval_ms | 지형, 그림자, 월드, 효과, 후처리, UI 사이의 GPU 타임스탬프 간격 |
| gpu_timeline_ms | 렌더링 시작부터 UI 제출 완료까지 GPU 타임라인 간격. SwapBuffers 대기는 제외 |
| counters.draw_calls | 실제 glDrawArrays + glDrawArraysInstanced API 호출 합계 |
| counters.quad_instances / glyph_instances | 제출된 도형 수 / 그중 글리프 수. draw_calls와 구분 |
| counters.upload_bytes / upload_calls / gpu_buffer_reuse | 버퍼·글리프 업로드와 GPU 배치 재사용 |
| counters.actors_total / actors_visible / actors_culled / sort_items | 그래프 규모와 가시성·정렬 작업량 |
| counters.model_cache_hit / model_cache_miss | 과거 스키마 호환용, 새 아틀라스 경로에서는 0 |
| counters.minimap_cache_hit / minimap_cache_miss / glyph_cache_miss | 캐시 동작 |
| counters.navigation_cells / collision_tests / enemies_updated / loot_updated | 게임플레이 작업량 |
| counters.gpu_sample_skipped / gpu_samples_pending | GPU 결과 지연 시 누락/대기 상황 |
| log_encode_ms / log_writer_ms_total | 렌더 스레드의 프레임 로그 인코딩 시간 / 저장 스레드 누적 작업 시간 |
| summary.fps / frame_ms_mean / frame_ms_p95 / frame_ms_max | 약 1초 구간의 간격 통계 |
| summary.working_set_bytes | 프로세스 메모리 표본. 실패 시 null. GPU VRAM 사용량은 아님 |
| scene_state | 월드 시드, 플레이어 위치, 적용 dt, 일시정지, 출력 해상도 |
| cpu_ms.npc_update / npc_think / npc_navigation | 주민 전체 갱신 / 목표 평가 / 목적지 A* 경로 계산 |
| cpu_ms.npc_combat / npc_trade / story_update / world_snapshot | 주민 전투 / 거래 / 소문·침수 상태 / 상태 JSON 인코딩 |
| counters.npcs_updated / npc_decisions / npc_path_cells / npc_path_failures | 갱신 인원·의사결정 수·탐색 셀·경로 실패 횟수 |
| counters.npc_combat_hits / npc_trades / npc_raid_coins / npc_deaths / story_events | 전투 처리·거래·약탈로 이동한 화폐·NPC 사망·사건 알림 |
| cpu_ms.chunk_visibility / counters.chunks_visible | 청크 그룹 가시성 판정 시간 / 해당 프레임 활성 가시 그룹 수(지형·환경 한 쌍을 1로 집계) |
| counters.world_props_added | 초기 생성 중 연결성을 보존하며 배치한 장애물 수 |
| counters.npc_path_reuse | 동일 목적지·동일 충돌 revision에서 재사용한 주민 경로 수 |
| counters.raid_attempts_blocked | 직업·보호 구역·재산·체력·영역·대기 조건으로 기각한 약탈 후보/처리 수 |
| cpu_ms.quest_update / counters.quest_transitions | 플레이어 의뢰 단계 평가 시간 / 단계 변경 및 명시적 행동 반응 수 |
| counters.terrain_tiles_changed | 수문 개방으로 실제 공유 지형 Actor가 물에서 길로 바뀐 타일 수 |
| counters.ui_labels_culled | 패널 또는 다른 이름표와 겹쳐 표시하지 않은 월드 이름표 수 |
| cpu_ms.monitor_snapshot | 게임 스레드의 모니터 JSON 생성·불변 문자열 게시 시간. world_snapshot을 포함하므로 두 구간을 더하지 않는다 |
| counters.world_snapshot_npcs | 파일 프로파일링 및 모니터 공용 월드 스냅샷에 인코딩한 NPC 표본 수. 고유 인구 수가 아님 |
| counters.monitor_snapshots / monitor_snapshot_bytes | 모니터에 게시한 표본 수 / 인코딩한 JSON bytes |
| counters.monitor_requests / monitor_response_bytes / monitor_errors | 네트워크 스레드에서 완료 후 게임 스레드가 회수한 요청 수 / 실제 전송한 HTTP 헤더+본문 bytes / 수신·전송·검증 오류 수 |
| counters.monitor_io_microseconds | 네트워크 요청 처리의 경과 microseconds. 소켓 대기를 포함하며 CPU 순수 실행 시간 또는 프레임 CPU 시간과 합산하지 않는다 |
| cpu_ms.terrain_appearance / counters.terrain_appearance_tiles | 생성·배수 후 표면 마스크/수심 재계산 시간 / 방문 지형 수. 프레임별 작업이 아님 |
| counters.shoreline_tiles | 해당 재계산에서 육지와 맞닿은 물 타일 수. 누적 카운트이므로 월드의 고유 개수와 구분 |
| cpu_ms.scenery_build / counters.scenery_props | 초기 물가/생활 소품 배치 시간 / 추가한 비충돌 소품 수 |
| cpu_ms.lighting / counters.lights_considered / counters.lights_selected | 프레임별 가시 광원 선별 시간 / 후보 수 / 실제 선택 수(최대 8) |
| counters.shadow_casters | 이번 프레임에 방향성 그림자와 접지 그림자를 생성한 Actor 수 |
| cpu_ms.character_build / counters.character_models_built | 과거 스키마 호환용, 오프라인 GLB 변환 비용은 게임 로그에 포함하지 않음 |
| counters.character_instances / mirrored_character_instances | 프레임에 제출한 플레이어/NPC 스프라이트 수 / 새 경로에서 반전은 사용하지 않아 0. 인구·드로우콜과 구분 |
| cpu_ms.resource_load | 시작 시 파일 읽기·검증·WIC 디코딩·GPU 아틀라스 업로드의 중첩 포함 CPU ms. 비동기 GPU 실행 시간은 아님 |
| counters.resource_bytes_read / resource_atlas_bytes | 읽은 PNG+메타데이터 bytes / 업로드한 RGBA bytes(현재 16,777,216). startup 값 |
| counters.resource_sprites_loaded | 불러온 모델 별칭 바인딩 수. 픽셀 공유 이미지 수나 Actor 수가 아님 |
| counters.resource_lookup_hit / resource_lookup_miss | 그리기 때 모델 조회 성공/실패 수. 정상 플레이의 miss는 0이어야 함 |
| cpu_ms.rumor_update / counters.rumor_candidates / rumor_transfers | 2초 간격 근접 소문 처리 시간 / 공간 버킷 내 후보 검사 수 / 새 지식 전달 메시지 수(지식 비트 수가 아님) |
| counters.courier_departures | 장터 체류를 마치고 다음 마을로 출발한 횟수. 최초 출발은 제외하며 고유 전달꾼 수가 아님 |
| cpu_ms.village_statistics / counters.village_mood_samples | 약 1초 간격 마을 분위기 집계 시간 / 집계한 마을 표본 수 |
| cpu_ms.enemy_respawn / counters.respawn_candidates / enemy_respawns | 몹 재생성 탐색 시간 / 검사한 캐시 후보 수 / 성공한 재생성 수 |

로컬 HTML 모니터는 WORLD_MONITOR.md를 따른다. 브라우저가 수신하는 모니터 스키마 1은
프로파일링 스키마 9과 별개다. 최대 1초 간격으로 게시하고, 구독이 5초간 없으면 생성하지 않는다.
일시정지 중에도 모니터 표본과 전송 통계는 갱신한다. 네트워크 스레드는 프로파일러의
프레임 배열에 직접 접근하지 않고 atomic 통계를 게임 스레드에서 회수한다.
world_state에도 이름·집계·최근 사건이 추가되며 영문 키와 단위는 기존 규약을 유지한다.

world_state.quest_step은 0~7(대화, 물건 회수, 사본, 선택, 수문, 완료 보고, 거점 탐험, 자유 탐험),
record_choice는 0=미결정, 1=공개, 2=관청, 3=판매다. quest_briefed/quest_returned/
hunter_camp_discovered는 플레이어가 해당 행동을 완료했는지 나타낸다.
quest_target_actor_id는 현재 목표 대상, quest_ledger_recovered/quest_key_recovered는 회수 수행 기록이다.
npcs.destination_x/destination_y는 이동 목적지(타일 좌표), path_remaining_waypoints는 남은 경유점 수다.
플레이어 선택 직후 다음 갱신에서 표본을 남겨 NPC의 실제 목적지 변화와 연결한다.
world_state.map_size_tiles는 한 변의 타일 수, npcs.home_x/home_y는 거점 좌표,
raid_cooldown_seconds는 재약탈 대기 초다. action=defend는 반격이며 재산을 빼앗지 않는다.
실제 약탈과 방어 전투를 action=raid / defend로 구분해 분석한다.
짐승의 플레이어 추격 길찾기는 반경 16의 지역 필드만 생성하며,
전체 월드 연결성 검사는 별도 비제한 탐색을 사용한다.

world_state는 진단용 전지적 표본이다. 게임 AI는 이 로그를 읽지 않으며 시야/대화/소문으로
얻은 knowledge_mask와 known_ledger_holder_actor_id, 마지막 목격 위치를 사용한다.
knowledge_mask 비트는 ledger=1, key=2, plans=4, published=8이다.
actor_id는 실행 내 식별자, definition_id는 0~99 인물 식별자다(0~4는 미라/도윤/세라트/이안/하람).
appearance_id 0~17은 공유 외형이며 인물 ID와 다르다. 등록 거주지는 home_settlement_id를 사용한다.
is_courier/travel_destination_settlement_id는 왕래 인원과 목적 마을, last_rumor_sender_actor_id/
last_rumor_world_minute/rumor_messages_received는 실제 근접 전달 이력이다.
settlements.mood_statistics.seconds/percent는 일시정지를 제외한 약 1초 표본의 세션 누적 시간·비율이다.
소문 처리는 story_update, 마을 집계는 npc_update, 몹 재생성은 ai의 자식 구간이므로 중첩 시간을 더하지 않는다.
world_minute는 첫날 자정부터의 누적 게임 분, health는 HP, coins는 화폐 수,
food/hides는 보유 수량, enlightenment/experience는 누적 성장 수치다.
affinity/witnessed_karma/faction_trust는 -100~100, 목표 weight는 무차원 상대 우선도다.
holder_actor_id=0은 땅에 놓인 물건이며 플레이어 소유권은 player_actor_id와 비교한다.
분석기의 last_world_state에 마지막 주민 상태가 포함된다. 돈은 거래·약탈에서 보존되지만,
사냥 전리품 생성, 조리(가죽 2개→보급품 1개), 음식·회복약 소비는 별도 자원 생성/소비다.

CPU 구간은 자식 시간을 포함한다. update + ai + navigation처럼 중첩된 값을 더하지 않는다.
scope_draw_calls 역시 중첩되므로 합산하지 않는다. 렌더 큐에 쌓인 도형의 실제 호출은
Flush가 일어난 구간에 귀속된다. model_build는 파일 캐시 초기 생성과 템플릿 변환 모두에 사용한다.

GPU는 8프레임 query ring에서 AVAILABLE을 확인한 뒤에만 결과를 읽는다.
결과가 늦으면 해당 프레임 계측을 건너뛰며 GL 결과를 강제로 기다리지 않는다.
미수신 결과는 0ms가 아니다. GPU timestamp 간격에는 CPU 명령 제출이 늦어 GPU가 쉰 시간도
들어갈 수 있으므로 GPU 사용률/순수 셰이더 실행 시간이라고 해석하면 안 된다.
frame_interval_ms는 현재 프레임에 이르기까지의 간격이고 cpu_ms는 현재 프레임 작업이므로
스파이크 상관관계 분석 시 이전 frame_id도 확인한다.

## 로그 분석

PowerShell 7에서 프로젝트 루트 기준:

```powershell
pwsh -File ./tools/Analyze-Profile.ps1 -Path "./BaseProject_SE/x64/Debug/Profiles/profile_*.jsonl"
pwsh -File ./tools/Analyze-Profile.ps1 -SelfTest
```

분석기는 세션별 평균, p95, 최악 프레임 10개, CPU/GPU 구간과 작업량을 JSON으로 출력한다.
마지막 줄이 잘린 로그는 invalid_json_lines로 집계하고 계속 읽는다. startup은 FPS에서 제외한다.
실행 중 복사한 파일은 저장 대기 이벤트가 빠질 수 있으므로 종료 후 분석을 권장한다.
이 도구의 self-test는 합성 데이터에 대한 분석기 검증이지 게임 성능 측정이 아니다.

## 다음 성능 검증

사용자가 Release/x64로 빌드 후 같은 월드 시드·해상도에서 정지, 이동, 전투, UI 열기,
일시정지, 창 크기 변경, 종료를 확인한다. Debug 기록은 별도로 비교한다.
프로파일링 ON/OFF를 비교해 계측 오버헤드도 확인한다.

새 시스템은 Perf::Scope와 Perf::Count로 계측한다. 파일 I/O나 신규 캐시는 hit/miss와 bytes를
함께 기록한다. 프레임마다 텍스트 콘솔 출력을 추가하지 않는다. 프로파일 API는 현재 게임/GL
스레드 전용이며, 향후 병렬 업데이트를 도입하면 스레드별 수집기를 추가해야 한다.
드라이버 내부 병목, GPU 캐시/대역폭, 개별 할당 콜스택과 아직 없는 시스템은 자동 분석하지 않는다.
필요 시 전용 GPU/CPU 프로파일러로 보완한다.

### 구현 참고

- [OpenGL 3.3 명세](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)
- [비동기 Query Object](https://wikis.khronos.org/opengl/Query_Object)
- [GDI 글리프 비트맵 형식](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getglyphoutlinea)
