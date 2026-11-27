-- BT4 Rankeds Details: Native Lua companion mod for BT4decomp.
-- Draws a live Time-Out tug-of-war bar under the battle timer in Team Battle / Tag Team.
-- 100% read-only and safe for online / Ranked matches.
-- (All combat/network telemetry is captured directly in C++ by winmm.dll.)

local max = math.max
local function clamp01(v) return v < 0 and 0 or v > 1 and 1 or v end

local state = {
    last_lead = 0,
    lead_pop_at = -10,
}

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

    local rMe = team_hp_ratio(fMe)
    local rOpp = team_hp_ratio(fOpp)
    local lossMe = max(0, 1 - rMe)
    local lossOpp = max(0, 1 - rOpp)
    local stake = lossMe + lossOpp
    local share = (stake > 0.0001) and clamp01(lossOpp / stake) or 0.5

    local lead = (rMe > rOpp + 0.0001) and 1 or ((rOpp > rMe + 0.0001) and -1 or 0)
    if lead ~= 0 and state.last_lead ~= 0 and lead ~= state.last_lead then
        state.lead_pop_at = game.time or 0
    end
    if lead ~= 0 then state.last_lead = lead end

    local now = game.time or 0
    local pop = (now - state.lead_pop_at < 0.3) and (1 + 0.16 * (1 - (now - state.lead_pop_at) / 0.3)) or 1
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
    version = "1.2.0",
    author = "BT4RankedsDetails",
    description = "Time-Out tug-of-war bar for Team Battle in BT4 Rankeds Details.",
    settings = {
        { id = "timeout_bar", label = "Time-Out bar (Team Battle)", type = "toggle", default = true,
          tip = "Shows who would win if time ran out right now under the timer." },
        { id = "timeout_1v1", label = "Show Time-Out bar in 1v1", type = "toggle", default = false },
        { id = "timeout_y", label = "Time-Out bar Y", type = "slider", min = 24, max = 120, step = 2, default = 46 },
    },

    on_hud = function(hud, game)
        if not game.fighters then return end
        if not (game.battle and game.hud_shown) then
            state.last_lead = 0
            return
        end
        local me = game.me or 1
        local opp = (me == 1) and 2 or 1
        draw_timeout_bar(hud, game, me, opp)
    end,
}
