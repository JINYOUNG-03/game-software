"use strict";

// Pure data helpers also used by the headless contract tests. Never inject game text as HTML.
const MonitorData = (() => {
    const jobs = { engineer: "수로 기술자", clerk: "조합 기록원", guard: "조사관", broker: "중개상",
        apprentice: "수습 수리공", hunter: "사냥꾼", raider: "약탈자", merchant: "상인", scholar: "학자", resident: "주민" };
    const actions = { rest: "몸 추스르기", hunt: "사냥", raid: "약탈", trade: "거래", study: "깨달음 찾기",
        investigate: "조사", return: "귀환", patrol: "순찰", dead: "사망", defend: "맞서기" };
    const moods = { conflict: "충돌 중", wounded: "부상자 발생", flooded: "침수 불안", controlled: "현장 통제",
        restless: "소문에 술렁임", quiet: "일상 유지", uninhabited: "상주 인원 없음" };
    const kinds = { village: "마을", outpost: "사냥꾼 거점", waystation: "주막", shrine: "산사", raider_camp: "약탈자 야영지" };
    const tendencies = { survival: "생존", wealth: "재산", enlightenment: "깨달음", raiding: "약탈", none: "집계 대상 없음" };
    const factions = ["관청", "조합", "주민", "외지인"];
    const number = value => Number.isFinite(value) ? value : 0;
    const signed = value => value > 0 ? `+${value}` : `${value}`;

    function validateSnapshot(data) {
        if (!data || data.schema_version !== 1 || data.status !== "ready" || data.event !== "monitor_snapshot" ||
            typeof data.session_id !== "string" || !Number.isFinite(data.sequence) || !Number.isFinite(data.uptime_ms) ||
            typeof data.paused !== "boolean" || !data.world || !Array.isArray(data.world.npcs) ||
            !Array.isArray(data.world.settlements) || !data.world.counts || !Array.isArray(data.world.recent_events)) {
            throw new Error("지원하지 않는 JSON 상태 형식입니다.");
        }
        const counts = data.world.counts;
        for (const key of ["npcs_total", "npcs_alive", "npcs_dead", "monsters_total", "monsters_alive", "monsters_dead_or_inactive"]) {
            if (!Number.isInteger(counts[key]) || counts[key] < 0) throw new Error(`개체 수 오류: ${key}`);
        }
        if (counts.npcs_total !== data.world.npcs.length || counts.npcs_alive + counts.npcs_dead !== counts.npcs_total ||
            counts.monsters_alive + counts.monsters_dead_or_inactive !== counts.monsters_total) {
            throw new Error("전체 개체 수와 생존/사망 집계가 일치하지 않습니다.");
        }
        for (const npc of data.world.npcs) {
            if (!Number.isSafeInteger(npc.actor_id) || typeof npc.name !== "string" ||
                !Number.isFinite(npc.health) || !Number.isFinite(npc.max_health) ||
                !Number.isFinite(npc.world_x) || !Number.isFinite(npc.world_y)) {
                throw new Error("NPC 상태 형식이 올바르지 않습니다.");
            }
        }
        for (const place of data.world.settlements) {
            if (typeof place.id !== "string" || typeof place.name !== "string" || !place.rumors ||
                !place.goal_weight_totals || !Array.isArray(place.faction_population)) {
                throw new Error("마을 상태 형식이 올바르지 않습니다.");
            }
        }
        return data;
    }

    function selectNpcs(npcs, filter) {
        const query = String(filter.query || "").trim().toLocaleLowerCase("ko-KR");
        return npcs.filter(npc => (!query || npc.name.toLocaleLowerCase("ko-KR").includes(query) || String(npc.actor_id).includes(query)) &&
            (!filter.job || npc.job === filter.job) && (!filter.action || npc.action === filter.action) &&
            (!filter.settlement || npc.home_settlement_id === filter.settlement) &&
            (filter.showDead !== false || npc.health > 0));
    }

    function knowledge(mask) {
        const value = number(mask);
        return [[1, "장부"], [2, "열쇠"], [4, "수문 계획"], [8, "공개 소식"]]
            .filter(([bit]) => value & bit).map(([, label]) => label).join(" · ") || "없음";
    }

    function goal(npc) {
        const weights = [["생존", number(npc.health_weight)], ["재산", number(npc.wealth_weight)],
            ["깨달음", number(npc.enlightenment_weight)], ["약탈", number(npc.raid_weight)]];
        weights.sort((a, b) => b[1] - a[1]);
        return `${weights[0][0]} (${weights[0][1].toFixed(1)})`;
    }

    function delta(current, previous, key) {
        if (!previous || !Number.isFinite(current[key]) || !Number.isFinite(previous[key])) return "";
        const change = current[key] - previous[key];
        return change ? signed(change) : "";
    }

    function settlementName(world, id) {
        return world.settlements.find(place => place.id === id)?.name || "마을 밖";
    }

    return { jobs, actions, moods, kinds, tendencies, factions, number, signed,
        validateSnapshot, selectNpcs, knowledge, goal, delta, settlementName };
})();
if (typeof module !== "undefined" && module.exports) module.exports = MonitorData;

if (typeof document !== "undefined") {
    const $ = id => document.getElementById(id);
    const D = MonitorData;
    let latest = null, previousNpcs = new Map(), selected = null;
    let stopped = false, inFlight = false, failures = 0, receivedAt = 0;
    let timer = null, controller = null;

    function element(tag, text, className) {
        const node = document.createElement(tag);
        if (text !== undefined) node.textContent = String(text);
        if (className) node.className = className;
        return node;
    }

    function status(text, kind) {
        $("connection").textContent = text;
        $("connection").className = `badge ${kind || ""}`;
    }

    function pair(list, key, value) {
        list.append(element("dt", key), element("dd", value));
    }

    function stat(label, value, description) {
        const node = element("article", undefined, "stat");
        node.append(element("span", label), element("strong", value), element("span", description));
        return node;
    }

    function fillOptions(id, values) {
        const select = $(id);
        const signature = JSON.stringify(values);
        if (select.dataset.options === signature) return;
        select.dataset.options = signature;
        const oldValue = select.value;
        const first = select.firstElementChild.cloneNode(true);
        select.replaceChildren(first);
        for (const [value, label] of values) {
            const option = element("option", label);
            option.value = value;
            select.append(option);
        }
        select.value = values.some(([value]) => value === oldValue) ? oldValue : "";
    }

    function renderSettlements(world) {
        const fragment = document.createDocumentFragment();
        for (const place of world.settlements) {
            const card = element("article", undefined, "settlement");
            const head = element("div", undefined, "settlement-head");
            head.append(element("h3", place.name), element("span", D.moods[place.mood] || place.mood, "badge"));
            card.append(head, element("p", D.kinds[place.kind] || place.kind, "muted"));
            const list = element("dl");
            pair(list, "거주 인구", `${place.population}명 · 사망 ${place.dead_residents}명`);
            pair(list, "현재 현장", `${place.npcs_present}명 · 외부 방문 ${place.visitors}명`);
            pair(list, "현장 활동", `거래 ${place.trading_present} · 충돌 ${place.conflicts_present} · 중상 ${place.wounded_present}`);
            pair(list, "거주민 재산", `${place.wealth_coins}냥`);
            pair(list, "주요 성향", D.tendencies[place.tendency] || place.tendency);
            pair(list, "세력 구성", place.faction_population.map((count, i) => `${D.factions[i] || i} ${count}`).join(" / "));
            pair(list, "침수", `${place.flood_percent}%`);
            card.append(list);
            for (const [key, label] of [["ledger_known", "장부 소문"], ["plans_known", "수문 계획"], ["published_known", "공개 소식"]]) {
                const row = element("div", undefined, "rumor");
                const meter = element("meter");
                meter.min = 0;
                meter.max = Math.max(1, D.number(place.population));
                meter.value = D.number(place.rumors[key]);
                meter.setAttribute("aria-label", `${place.name} ${label} 보유 인원`);
                row.append(element("span", label), meter, element("span", `${place.rumors[key]}/${place.population}`));
                card.append(row);
            }
            const weights = place.goal_weight_totals;
            card.append(element("p", `목표 합계 · 생존 ${D.number(weights.health).toFixed(1)} / 재산 ${D.number(weights.wealth).toFixed(1)} / 깨달음 ${D.number(weights.enlightenment).toFixed(1)} / 약탈 ${D.number(weights.raid).toFixed(1)}`, "muted"));
            fragment.append(card);
        }
        $("settlements").replaceChildren(fragment);
    }

    function renderDetail(world) {
        const panel = $("npc-detail");
        const npc = world.npcs.find(person => person.actor_id === selected);
        if (!npc) {
            panel.textContent = "NPC를 선택하세요.";
            return;
        }
        const list = element("dl");
        const target = world.npcs.find(person => person.actor_id === npc.target_actor_id);
        const targetName = target ? target.name : npc.target_actor_id === world.player_actor_id ? "플레이어" :
            npc.target_actor_id ? `Actor ${npc.target_actor_id}` : "없음";
        pair(list, "대상", targetName);
        pair(list, "이동", `현재 (${npc.world_x.toFixed(1)}, ${npc.world_y.toFixed(1)}) → 목적지 (${D.number(npc.destination_x).toFixed(1)}, ${D.number(npc.destination_y).toFixed(1)}) · 남은 경유점 ${npc.path_remaining_waypoints}`);
        pair(list, "소지품", `가죽 ${npc.hides} · 식량 ${npc.food} · 장부 사본 ${npc.has_ledger_copy ? "있음" : "없음"}`);
        pair(list, "성장", `경험치 ${npc.experience} · 깨달음 ${npc.enlightenment}`);
        pair(list, "목표 가중치", `생존 ${npc.health_weight} / 재산 ${npc.wealth_weight} / 깨달음 ${npc.enlightenment_weight} / 약탈 ${npc.raid_weight}`);
        pair(list, "태도 / 목격 평가", `플레이어 친밀도 ${npc.affinity} / 목격한 행위 평가 ${npc.witnessed_karma}`);
        pair(list, "약탈 대기", `${npc.raid_cooldown_seconds}초`);
        pair(list, "보유 지식", D.knowledge(npc.knowledge_mask));
        panel.replaceChildren(element("h3", `${npc.name} · Actor ${npc.actor_id}`), list, element("p", npc.dialogue || "대사 없음"));
    }

    function renderNpcs() {
        if (!latest) return;
        const world = latest.world;
        const filtered = D.selectNpcs(world.npcs, { query: $("search").value, job: $("job").value,
            action: $("action").value, settlement: $("settlement").value, showDead: $("show-dead").checked });
        $("npc-count").textContent = `${filtered.length} / ${world.npcs.length}명`;
        const rows = document.createDocumentFragment();
        for (const npc of filtered) {
            const row = element("tr");
            row.className = [npc.health <= 0 ? "dead" : "", npc.actor_id === selected ? "selected" : ""].join(" ");
            const cell = (text, secondary) => {
                const td = element("td", text);
                if (secondary) td.append(element("small", secondary));
                row.append(td);
                return td;
            };
            const nameCell = cell("");
            const button = element("button", npc.name);
            button.type = "button";
            button.setAttribute("aria-pressed", String(npc.actor_id === selected));
            button.addEventListener("click", () => { selected = npc.actor_id; renderNpcs(); $("npc-detail").scrollIntoView({ block: "nearest" }); });
            nameCell.append(button, element("small", `#${npc.actor_id}${npc.health <= 0 ? " · 사망" : ""}${npc.active === false ? " · 비활성" : ""}`));
            cell(D.jobs[npc.job] || npc.job, D.settlementName(world, npc.home_settlement_id));
            cell(D.actions[npc.action] || npc.action, `${D.settlementName(world, npc.current_settlement_id)} (${npc.world_x.toFixed(1)}, ${npc.world_y.toFixed(1)})`);
            const hp = cell(`${npc.health}/${npc.max_health}`);
            const previous = previousNpcs.get(npc.actor_id);
            hp.append(element("span", D.delta(npc, previous, "health"), "delta"));
            const coins = cell(`${npc.coins}냥`);
            coins.append(element("span", D.delta(npc, previous, "coins"), "delta"));
            cell(D.goal(npc));
            cell(D.knowledge(npc.knowledge_mask));
            rows.append(row);
        }
        if (!filtered.length) {
            const cell = element("td", "조건에 맞는 NPC가 없습니다.");
            cell.colSpan = 7;
            const row = element("tr");
            row.append(cell);
            rows.append(row);
        }
        $("npcs").replaceChildren(rows);
        renderDetail(world);
    }

    function render(data) {
        const world = data.world, counts = world.counts;
        const fps = data.performance?.fps;
        $("overview").replaceChildren(
            stat("살아 있는 NPC", counts.npcs_alive, `전체 ${counts.npcs_total} · 사망 ${counts.npcs_dead}`),
            stat("살아 있는 몹", counts.monsters_alive, `배치 슬롯 ${counts.monsters_total} · 사망/비활성 ${counts.monsters_dead_or_inactive}`),
            stat("마을 / 기타 거점", `${world.settlements.filter(place => place.kind === "village").length} / ${world.settlements.filter(place => place.kind !== "village").length}`, "상주 인원이 없는 장소도 표시"),
            stat("게임 상태", data.paused ? "일시정지" : "진행 중", world.clock),
            stat("프레임 속도", Number.isFinite(fps) ? `${fps.toFixed(1)} FPS` : "측정 중", `평균 드로우콜 ${Number.isFinite(data.performance?.draw_calls_mean) ? data.performance.draw_calls_mean.toFixed(0) : "미수집"}`));
        $("objective").textContent = world.objective;
        $("consequence").textContent = `내 선택의 결과 · ${world.consequence}`;
        $("world-status").textContent = `시드 ${world.seed ?? "—"} · ${world.map_size_tiles}×${world.map_size_tiles} 타일 · 플레이어 HP ${world.player_health} / ${world.player_coins}냥 · 수문 ${world.gate_open ? "열림" : "닫힘"} · 현장 ${world.drain_controlled ? "통제" : "개방"} · 세력 신뢰 ${(world.faction_trust || []).map((trust, i) => `${D.factions[i]} ${trust}`).join(" / ")}`;
        renderSettlements(world);
        fillOptions("job", Object.entries(D.jobs));
        fillOptions("action", Object.entries(D.actions));
        fillOptions("settlement", [...world.settlements.map(place => [place.id, place.name]), ["wilderness", "마을 밖"]]);
        renderNpcs();
        $("events").replaceChildren(...world.recent_events.map(text => element("li", text)));
        if (!world.recent_events.length) $("events").append(element("li", "기록된 사건이 없습니다."));
        if ($("json-panel").open) $("json").textContent = JSON.stringify(data, null, 2);
        $("download").disabled = false;
    }

    async function poll() {
        clearTimeout(timer);
        if (stopped || inFlight) return;
        inFlight = true;
        controller = new AbortController();
        const timeout = setTimeout(() => controller?.abort(), 2500);
        try {
            const response = await fetch("/api/snapshot", { cache: "no-store", signal: controller.signal });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const data = await response.json();
            if (stopped) return;
            if (data.status === "waiting") {
                status("첫 상태 준비 중");
                $("notice").textContent = "게임에 연결됐습니다. 다음 프레임에서 첫 상태를 준비합니다.";
            } else {
                D.validateSnapshot(data);
                if (latest?.session_id !== data.session_id || latest.sequence !== data.sequence) {
                    previousNpcs = latest?.session_id === data.session_id ? new Map(latest.world.npcs.map(npc => [npc.actor_id, npc])) : new Map();
                    if (latest?.session_id !== data.session_id) selected = null;
                    latest = data;
                    receivedAt = Date.now();
                    render(data);
                }
                const stale = Date.now() - receivedAt > 5000;
                status(stale ? "갱신 지연" : "연결됨", stale ? "stale" : "live");
                $("notice").textContent = stale ? "연결은 됐지만 게임 상태가 갱신되지 않습니다. 게임 창이 응답하는지 확인하세요." :
                    "상태를 받고 있습니다. 이 화면은 게임 데이터를 조회하며 NPC 행동을 변경하지 않습니다.";
            }
            failures = 0;
        } catch (error) {
            if (!stopped) {
                failures++;
                status("연결 끊김 · 재시도", "stale");
                $("notice").textContent = `게임 실행과 주소를 확인하세요. 마지막 상태는 유지됩니다. (${error.message})`;
            }
        } finally {
            clearTimeout(timeout);
            controller = null;
            inFlight = false;
            if (!stopped) timer = setTimeout(poll, failures ? Math.min(8000, 1000 * 2 ** failures) : 1000);
        }
    }

    for (const id of ["search", "job", "action", "settlement", "show-dead"]) $(id).addEventListener("input", renderNpcs);
    $("toggle").addEventListener("click", () => {
        stopped = !stopped;
        $("toggle").textContent = stopped ? "수신 재개" : "수신 일시정지";
        if (stopped) {
            clearTimeout(timer);
            controller?.abort();
            status("수신 일시정지");
            $("notice").textContent = "마지막 수신 상태를 보고 있습니다. 게임은 계속 진행됩니다.";
        } else poll();
    });
    $("retry").addEventListener("click", () => {
        stopped = false;
        failures = 0;
        $("toggle").textContent = "수신 일시정지";
        poll();
    });
    $("download").addEventListener("click", () => {
        if (!latest) return;
        const url = URL.createObjectURL(new Blob([JSON.stringify(latest, null, 2)], { type: "application/json" }));
        const link = element("a");
        link.href = url;
        link.download = `muyeon-${latest.session_id}-${latest.sequence}.json`;
        document.body.append(link);
        link.click();
        link.remove();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    });
    $("json-panel").addEventListener("toggle", () => {
        $("json").textContent = $("json-panel").open && latest ? JSON.stringify(latest, null, 2) : "";
    });
    const ageTimer = setInterval(() => {
        if (latest && receivedAt) $("received").textContent = `#${latest.sequence} · 마지막 갱신 ${Math.floor((Date.now() - receivedAt) / 1000)}초 전`;
    }, 1000);
    window.addEventListener("pagehide", () => { stopped = true; clearTimeout(timer); clearInterval(ageTimer); controller?.abort(); });
    if (location.protocol === "file:") {
        stopped = true;
        status("로컬 주소로 열어주세요", "stale");
        $("notice").textContent = "HTML 파일을 직접 여는 대신 게임 실행 후 http://127.0.0.1:18765/ 로 접속하세요.";
    } else poll();
}
