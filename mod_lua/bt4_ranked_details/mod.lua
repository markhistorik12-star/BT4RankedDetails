-- BT4 Ranked Details: Native Lua companion mod for BT4decomp.
-- 1) Draws a live Time-Out tug-of-war bar under the battle timer in Team Battle / Tag Team.
-- 2) Emits compact read-only telemetry lines (BT4RD|...) via bt3.log() so winmm.dll can
--    record combat stats (damage, healing, combos, Max Power, counters, switches) and HP timelines.
-- 100% read-only and safe for online / Ranked matches.

local floor, abs, min, max = math.floor, math.abs, math.min, math.max
local function clamp01(v) return v < 0 and 0 or v > 1 and 1 or v end

local state = {
    in_battle = false,
    last_hp_log = 0,
    last_time = 0,
    last_lead = 0,
    lead_pop_at = -10,
    side = { {}, {} },
}

local function reset_side()
    return {
        chara = -1,
        active = -1,
        hp = -1,
        dmg = 0,
        healed = 0,
        sparking_s = 0,
        charge_s = 0,
        was_sparking = false,
        hit_seq = -1,
        msg_seq = -1,
        last_combo_hits = 0,
        last_combo_dmg = 0,
        switch_grace_until = 0,
    }
end

local function side_tag(idx, me)
    return (idx == me) and "me" or "opp"
end

local function team_csv(f)
    if not f then return "" end
    local ids = {}
    if f.members and #f.members > 0 then
        for _, m in ipairs(f.members) do
            if m.present and m.chara ~= nil then
                ids[#ids + 1] = tostring(m.chara)
            end
        end
    elseif f.chara ~= nil then
        ids[1] = tostring(f.chara)
    end
    return table.concat(ids, ",")
end

local function team_hp_ratio(f)
    if not f then return 0, 0 end
    local cur, tot, alive = 0, 0, 0
    if f.members and #f.members > 0 then
        for _, m in ipairs(f.members) do
            if m.present and (m.hp_max or 0) > 0 then
                local h = max(0, m.hp or 0)
                cur = cur + h
                tot = tot + m.hp_max
                if h > 0 then alive = alive + 1 end
            end
        end
    elseif (f.hp_max or 0) > 0 then
        cur = max(0, f.hp or 0)
        tot = f.hp_max
        alive = cur > 0 and 1 or 0
    end
    return (tot > 0) and (cur / tot) or 0, alive
end

local function draw_timeout_bar(hud, game, me, opp)
    if not bt3.setting("timeout_bar") then return end
    local fMe, fOpp = game.fighters[me], game.fighters[opp]
    if not fMe or not fOpp then return end
    local is_team = (fMe.members and #fMe.members > 1) or (fOpp.members and #fOpp.members > 1)
    if not is_team and not bt3.setting("timeout_1v1") then return end

    local rMe, _ = team_hp_ratio(fMe)
    local rOpp, _ = team_hp_ratio(fOpp)
    local lossMe = max(0, 1 - rMe)
    local lossOpp = max(0, 1 - rOpp)
    local stake = lossMe + lossOpp
    local share = 0.5
    if stake > 0.0001 then
        share = clamp01(lossOpp / stake)
    end

    local lead = (rMe > rOpp + 0.0001) and 1 or ((rOpp > rMe + 0.0001) and -1 or 0)
    if lead ~= 0 and state.last_lead ~= 0 and lead ~= state.last_lead then
        state.lead_pop_at = game.time
    end
    if lead ~= 0 then state.last_lead = lead end

    local pop = (game.time - state.lead_pop_at < 0.3) and (1 + 0.16 * (1 - (game.time - state.lead_pop_at) / 0.3)) or 1
    local U = hud.h / 448
    local w = 160 * U * pop
    local h = 7 * U * pop
    local x = (hud.w - w) * 0.5
    local y = (bt3.setting("timeout_y") or 46) * U

    hud.fill(x - 2, y - 2, w + 4, h + 4, bt3.rgba(8, 12, 22, 180), 3)
    local splitX = x + w * share
    local colMe  = (lead >= 0) and bt3.rgba(72, 220, 120, 235) or bt3.rgba(120, 130, 145, 180)
    local colOpp = (lead < 0)  and bt3.rgba(235, 78, 72, 235)  or bt3.rgba(120, 130, 145, 180)
    if share > 0.005 then hud.fill(x, y, w * share, h, colMe, 2) end
    if share < 0.995 then hud.fill(splitX, y, w * (1 - share), h, colOpp, 2) end
    -- 50% center notch
    hud.fill(x + w * 0.5 - 1, y - 2, 2, h + 4, bt3.rgba(255, 255, 255, 230))
end

return {
    name = "BT4 Rankeds Details",
    version = "1.1.0",
    author = "BT4RankedsDetails",
    description = "Time-Out tug-of-war bar for Team Battle in BT4 Rankeds Details.",
    settings = {
        { id = "timeout_bar", label = "Time-Out bar (Team Battle)", type = "toggle", default = true,
          tip = "Shows who would win if time ran out right now under the timer." },
        { id = "timeout_1v1", label = "Show Time-Out bar in 1v1", type = "toggle", default = false },
        { id = "timeout_y", label = "Time-Out bar Y", type = "slider", min = 24, max = 120, step = 2, default = 46 },
        { id = "log_telemetry", label = "Export combat telemetry", type = "toggle", default = true,
          tip = "Writes compact BT4RD| lines to bt3_log.txt for the history viewer." },
    },

    on_hud = function(hud, game)
        if not game.fighters then return end
        local me = game.me or 1
        local opp = (me == 1) and 2 or 1

        if game.battle and game.hud_shown then
            draw_timeout_bar(hud, game, me, opp)
        end

        if not bt3.setting("log_telemetry") then return end
        if not game.online then return end

        local now = game.time or 0
        local dt = (state.last_time > 0 and now >= state.last_time) and min(0.25, now - state.last_time) or 0
        state.last_time = now

        if game.battle and not state.in_battle then
            state.in_battle = true
            state.last_hp_log = now
            state.last_lead = 0
            state.side[1] = reset_side()
            state.side[2] = reset_side()
            bt3.log("BT4RD|TEAM|me|" .. team_csv(game.fighters[me]) .. "|" .. team_csv(game.fighters[opp]))
        elseif not game.battle and state.in_battle then
            state.in_battle = false
            local sMe, sOpp = state.side[me], state.side[opp]
            local rMe, aMe = team_hp_ratio(game.fighters[me])
            local rOpp, aOpp = team_hp_ratio(game.fighters[opp])
            local res = "unknown"
            local etype = "KnockOut"
            if aMe > 0 and aOpp == 0 then res = "win"
            elseif aOpp > 0 and aMe == 0 then res = "loss"
            elseif rMe > rOpp + 0.001 then res = "win"; etype = "TimeUp"
            elseif rOpp > rMe + 0.001 then res = "loss"; etype = "TimeUp"
            elseif aMe == 0 and aOpp == 0 then res = "draw"; etype = "DoubleKnockOut" end
            bt3.log(string.format("BT4RD|SUM|%d|%d|%d|%d|%.1f|%.1f|%.1f|%.1f|%s|%s",
                floor(sMe.dmg), floor(sOpp.dmg), floor(sMe.healed), floor(sOpp.healed),
                sMe.sparking_s, sOpp.sparking_s, sMe.charge_s, sOpp.charge_s, res, etype))
            return
        end

        if not state.in_battle then return end

        for idx = 1, 2 do
            local f = game.fighters[idx]
            local s = state.side[idx]
            local tag = side_tag(idx, me)
            if f then
                local hp = max(0, f.hp or 0)
                local ch = f.chara or -1
                local act = f.active or 1

                if s.active >= 0 and act ~= s.active then
                    s.switch_grace_until = now + 2.0
                    bt3.log("BT4RD|EV|" .. tag .. "|switch|" .. tostring(ch))
                elseif s.chara >= 0 and ch ~= s.chara then
                    s.switch_grace_until = now + 2.0
                end

                if s.hp >= 0 and now > s.switch_grace_until then
                    local d = hp - s.hp
                    if d < 0 then
                        local other = state.side[(idx == 1) and 2 or 1]
                        other.dmg = other.dmg + (-d)
                    elseif d > 0 and d < 15000 then
                        s.healed = s.healed + d
                    end
                end
                s.hp, s.chara, s.active = hp, ch, act

                if f.powered_up then
                    s.sparking_s = s.sparking_s + dt
                    if not s.was_sparking then
                        bt3.log("BT4RD|EV|" .. tag .. "|sparking|Max Power")
                    end
                end
                s.was_sparking = (f.powered_up == true)

                if f.charging then s.charge_s = s.charge_s + dt end

                -- Combo tracking
                if f.hit_seq ~= nil and f.hit_seq ~= s.hit_seq then
                    if s.last_combo_hits >= 2 and (f.hud_hits or 0) < s.last_combo_hits then
                        bt3.log(string.format("BT4RD|COMBO|%s|%d|%d", tag, s.last_combo_hits, floor(s.last_combo_dmg)))
                    end
                    s.hit_seq = f.hit_seq
                    s.last_combo_hits = f.hud_hits or 0
                    s.last_combo_dmg = max(0, f.hud_damage or 0)
                end

                -- Game event messages (0=Counter, 1=MAX POWER, 3=First Attack, 4=BOOST)
                if f.message_seq ~= nil and f.message_seq ~= s.msg_seq then
                    if s.msg_seq >= 0 then
                        local m = f.message
                        if m == 0 then bt3.log("BT4RD|EV|" .. tag .. "|counter|Counter!")
                        elseif m == 3 then bt3.log("BT4RD|EV|" .. tag .. "|first_attack|First Attack!")
                        elseif m == 4 then bt3.log("BT4RD|EV|" .. tag .. "|boost|Boost!") end
                    end
                    s.msg_seq = f.message_seq
                end
            end
        end

        -- Periodic HP sample for timeline (every 0.5 s)
        if now - state.last_hp_log >= 0.5 then
            state.last_hp_log = now
            local fMe, fOpp = game.fighters[me], game.fighters[opp]
            if fMe and fOpp then
                bt3.log(string.format("BT4RD|HP|%d|%d|%d|%d",
                    max(0, fMe.hp or 0), max(0, fOpp.hp or 0), fMe.chara or 0, fOpp.chara or 0))
            end
        end
    end,
}
