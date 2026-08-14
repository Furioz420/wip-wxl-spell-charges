// Native presentation for WarcraftXL's server-authoritative spell charges.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"
#include "game/Script.hpp"
#include "wxl/WxlOpcodes.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <vector>

namespace
{
    namespace script = wxl::game::script;

    namespace opcodes
    {
        constexpr uint16_t CmsgSpellChargesRequest =
            WXL_CMSG_SPELL_CHARGES_REQUEST;
        constexpr uint16_t SmsgSpellChargesUpdate =
            WXL_SMSG_SPELL_CHARGES_UPDATE;
    }

    namespace network
    {
        bool RegisterClientOpcode(uint16_t opcode, const char* name)
        {
            const WXL_NetworkApi* api = wxl_spell_charges::Network();
            return api && api->RegisterClientOpcode(opcode, name) != 0;
        }

        bool RegisterServerOpcode(uint16_t opcode, const char* name,
                                  WXL_NetworkPacketHandler handler)
        {
            const WXL_NetworkApi* api = wxl_spell_charges::Network();
            return api && api->RegisterServerOpcode(
                opcode, name, handler, nullptr) != 0;
        }

        bool Send(uint16_t opcode)
        {
            const WXL_NetworkApi* api = wxl_spell_charges::Network();
            return api && api->Send(opcode, nullptr, 0) != 0;
        }
    }

    namespace framescript
    {
        bool RegisterFunction(const char* name, WXL_LuaCFunction function)
        {
            const WXL_FrameScriptApi* api = wxl_spell_charges::FrameScript();
            return api && api->RegisterFunction(name, function) != 0;
        }

        bool RegisterScript(const char* name, const char* source)
        {
            const WXL_FrameScriptApi* api = wxl_spell_charges::FrameScript();
            return api && api->RegisterScript(name, source) != 0;
        }

        bool ExecuteCurrent(const char* name, const char* source)
        {
            const WXL_FrameScriptApi* api = wxl_spell_charges::FrameScript();
            return api && api->ExecuteCurrent(name, source) != 0;
        }
    }

    constexpr uint8_t kProtocolVersion = 1;
    constexpr size_t kHeaderBytes = 3;
    constexpr size_t kRowBytes = 18;
    constexpr size_t kMaxPayloadBytes = 64u * 1024u;
    constexpr uint32_t kRequestThrottleMs = 250;

    struct ChargeRow
    {
        uint32_t spellId = 0;
        uint32_t categoryId = 0;
        uint8_t maximum = 0;
        uint8_t current = 0;
        uint32_t rechargeDurationMs = 0;
        uint32_t rechargeRemainingMs = 0;
        uint32_t receivedAtMs = 0;
    };

    struct Reader
    {
        const uint8_t* data = nullptr;
        size_t size = 0;
        size_t cursor = 0;

        template <typename T>
        bool Read(T& value)
        {
            if (!data || cursor > size || sizeof(T) > size - cursor)
                return false;
            std::memcpy(&value, data + cursor, sizeof(T));
            cursor += sizeof(T);
            return true;
        }
    };

    std::mutex g_chargeMutex;
    std::vector<ChargeRow> g_charges;
    uint32_t g_generation = 0;
    uint32_t g_lastRequestMs = 0;

    uint32_t RemainingMs(const ChargeRow& row, uint32_t now) noexcept
    {
        if (!row.rechargeRemainingMs) return 0;
        const uint32_t elapsed = now - row.receivedAtMs;
        return elapsed >= row.rechargeRemainingMs
            ? 0u : row.rechargeRemainingMs - elapsed;
    }

    bool ParseSnapshot(std::span<const uint8_t> payload)
    {
        if (payload.size() < kHeaderBytes ||
            payload.size() > kMaxPayloadBytes)
            return false;

        Reader reader{payload.data(), payload.size()};
        uint8_t version = 0;
        uint16_t count = 0;
        if (!reader.Read(version) || !reader.Read(count) ||
            version != kProtocolVersion ||
            payload.size() != kHeaderBytes + size_t(count) * kRowBytes)
            return false;

        std::vector<ChargeRow> replacement;
        replacement.reserve(count);
        const uint32_t receivedAt = GetTickCount();
        for (uint16_t index = 0; index < count; ++index)
        {
            ChargeRow row;
            if (!reader.Read(row.spellId) ||
                !reader.Read(row.categoryId) ||
                !reader.Read(row.maximum) ||
                !reader.Read(row.current) ||
                !reader.Read(row.rechargeDurationMs) ||
                !reader.Read(row.rechargeRemainingMs))
                return false;

            if (!row.spellId || !row.maximum) continue;
            row.current = std::min(row.current, row.maximum);
            row.receivedAtMs = receivedAt;
            replacement.push_back(row);
        }
        if (reader.cursor != reader.size) return false;

        {
            const std::lock_guard lock(g_chargeMutex);
            g_charges = std::move(replacement);
            ++g_generation;
            if (!g_generation) ++g_generation;
        }
        return true;
    }

    void __cdecl OnSnapshot(
        const uint8_t* data, uint32_t size, void*)
    {
        const std::span<const uint8_t> payload(data, size);
        if (!ParseSnapshot(payload))
        {
            WLOG_WARN(
                "spell-charges: rejected malformed snapshot bytes=%zu",
                payload.size());
            return;
        }

        framescript::ExecuteCurrent(
            "spell-charges-update",
            "if WXLSpellCharges and WXLSpellCharges._NativeChanged then "
            "WXLSpellCharges._NativeChanged() end");
    }

    bool SendRequest()
    {
        const uint32_t now = GetTickCount();
        if (g_lastRequestMs && now - g_lastRequestMs < kRequestThrottleMs)
            return true;
        if (!network::Send(opcodes::CmsgSpellChargesRequest)) return false;
        g_lastRequestMs = now;
        return true;
    }

    int __cdecl LuaRequest(void* state)
    {
        script::PushBoolean(state, SendRequest());
        return 1;
    }

    int __cdecl LuaSnapshotInfo(void* state)
    {
        size_t count = 0;
        uint32_t generation = 0;
        {
            const std::lock_guard lock(g_chargeMutex);
            count = g_charges.size();
            generation = g_generation;
        }

        script::PushNumber(state, static_cast<double>(count));
        script::PushNumber(state, static_cast<double>(generation));
        return 2;
    }

    int __cdecl LuaSnapshotRow(void* state)
    {
        if (!script::IsNumber(state, 1))
        {
            script::PushNil(state);
            return 1;
        }

        const double indexNumber = script::ToNumber(state, 1);
        if (!std::isfinite(indexNumber) || indexNumber < 1.0 ||
            indexNumber >
                static_cast<double>((std::numeric_limits<uint16_t>::max)()))
        {
            script::PushNil(state);
            return 1;
        }

        ChargeRow row;
        {
            const std::lock_guard lock(g_chargeMutex);
            const size_t index = static_cast<size_t>(indexNumber - 1.0);
            if (index >= g_charges.size())
            {
                script::PushNil(state);
                return 1;
            }
            row = g_charges[index];
        }

        const uint32_t remainingMs = RemainingMs(row, GetTickCount());
        script::PushNumber(state, row.spellId);
        script::PushNumber(state, row.categoryId);
        script::PushNumber(state, row.maximum);
        script::PushNumber(state, row.current);
        script::PushNumber(state, row.rechargeDurationMs);
        script::PushNumber(state, remainingMs);
        return 6;
    }

    constexpr char kBootstrap[] = R"lua(
do
    wxlwow = wxlwow or {}
    wxlwow.request_spell_charges = _WXLWOW_REQUEST_SPELL_CHARGES
    wxlwow.spell_charge_snapshot = _WXLWOW_SPELL_CHARGE_SNAPSHOT
    wxlwow.spell_charge_row = _WXLWOW_SPELL_CHARGE_ROW
    _WXLWOW_REQUEST_SPELL_CHARGES = nil
    _WXLWOW_SPELL_CHARGE_SNAPSHOT = nil
    _WXLWOW_SPELL_CHARGE_ROW = nil

    local GCD_SPELL = 61304
    local SC = _G.WXLSpellCharges or {}
    _G.WXLSpellCharges = SC
    SC.spells = SC.spells or {}
    SC.revision = SC.revision or 0
    SC.nativeRevision = SC.nativeRevision or -1

    if not SC.nativeGetActionCount then
        SC.nativeGetActionCount = GetActionCount
        SC.nativeGetActionCooldown = GetActionCooldown
        SC.nativeIsStackableAction = IsStackableAction
        SC.nativeIsUsableAction = IsUsableAction
    end

    local buttonPrefixes = {
        "ActionButton", "BonusActionButton", "MultiBarBottomLeftButton",
        "MultiBarBottomRightButton", "MultiBarLeftButton", "MultiBarRightButton"
    }

    local function dataForAction(action)
        if not action or not HasAction(action) then return nil end
        local kind, actionId, subType, explicitSpellId = GetActionInfo(action)
        if kind ~= "spell" then return nil end
        actionId = tonumber(actionId)
        explicitSpellId = tonumber(explicitSpellId)
        if explicitSpellId and SC.spells[explicitSpellId] then
            return SC.spells[explicitSpellId]
        end
        if actionId then return SC.spells[actionId] end
        return nil
    end

    local function recharge(data)
        local now = GetTime()
        if not data or data.current >= data.maximum or
           not data.rechargeEnd or data.rechargeEnd <= now or
           data.durationMs <= 0 then
            return nil
        end
        local duration = data.durationMs / 1000
        return data.rechargeEnd - duration, duration, 1
    end

    local function gcd(action)
        local start, duration, enable
        if action then
            start, duration, enable = SC.nativeGetActionCooldown(action)
        end
        if start and duration and enable ~= 0 and start > 0 and
           duration > 0 and duration <= 2.5 and
           start + duration > GetTime() then
            return start, duration, enable
        end
        if GetSpellCooldown then
            start, duration, enable = GetSpellCooldown(GCD_SPELL)
            if start and duration and enable ~= 0 and start > 0 and
               duration > 0 and duration <= 2.5 and
               start + duration > GetTime() then
                return start, duration, enable
            end
        end
        return nil
    end

    local function buttonAction(button)
        if not button then return nil end
        local action = button.action
        if not action and button.GetAttribute then
            action = button:GetAttribute("action")
        end
        if not action and ActionButton_GetPagedID then
            action = ActionButton_GetPagedID(button)
        end
        return action
    end

    local function updateButton(button)
        if not button then return end
        local action = buttonAction(button)
        local data = dataForAction(action)
        if not data and not button.wxlCharges then return end
        local name = button:GetName()
        local count = name and _G[name .. "Count"]
        local cooldown = name and _G[name .. "Cooldown"]
        local icon = name and _G[name .. "Icon"]

        if not data then
            if count and button.wxlCharges then count:Hide() end
            if icon and button.wxlCharges then
                icon:SetVertexColor(1.0, 1.0, 1.0)
            end
            button.wxlCharges = nil
            return
        end

        button.wxlCharges = true
        if count and data.maximum > 1 then
            count:SetText(tostring(data.current))
            count:Show()
        elseif count then
            count:Hide()
        end

        local start, duration, enable = gcd(action)
        if not start then start, duration, enable = recharge(data) end
        if cooldown and start then
            if CooldownFrame_SetTimer then
                CooldownFrame_SetTimer(cooldown, start, duration, enable)
            else
                cooldown:SetCooldown(start, duration)
            end
            cooldown:Show()
        elseif cooldown then
            cooldown:Hide()
        end

        if icon then
            local shade = data.current > 0 and 1.0 or 0.4
            icon:SetVertexColor(shade, shade, shade)
        end
    end

    local function updateBars()
        for _, prefix in ipairs(buttonPrefixes) do
            for index = 1, 12 do
                updateButton(_G[prefix .. index])
            end
        end
    end

    local function syncSnapshot(force)
        if not wxlwow.spell_charge_snapshot or
           not wxlwow.spell_charge_row then return false end
        local count, revision = wxlwow.spell_charge_snapshot()
        count = tonumber(count) or 0
        revision = tonumber(revision) or 0
        if not force and revision == SC.nativeRevision then return false end

        local nextState = {}
        local now = GetTime()
        for index = 1, count do
            local spellId, groupId, maximum, current, durationMs, remainingMs =
                wxlwow.spell_charge_row(index)
            if spellId and maximum and maximum > 0 then
                current = math.max(0, math.min(maximum, current or 0))
                nextState[spellId] = {
                    spellId = spellId,
                    groupId = groupId or 0,
                    maximum = maximum,
                    current = current,
                    durationMs = durationMs or 0,
                    rechargeEnd = remainingMs and remainingMs > 0 and
                        (now + remainingMs / 1000) or 0
                }
            end
        end
        SC.spells = nextState
        SC.nativeRevision = revision
        SC.revision = SC.revision + 1
        return true
    end

    SC._NativeChanged = function()
        if syncSnapshot(true) then updateBars() end
    end

    SC.Request = function()
        if wxlwow.request_spell_charges then
            return wxlwow.request_spell_charges()
        end
        return false
    end

    GetActionCount = function(action)
        local data = dataForAction(action)
        if data then return data.current end
        return SC.nativeGetActionCount(action)
    end

    IsStackableAction = function(action)
        if dataForAction(action) then return 1 end
        return SC.nativeIsStackableAction(action)
    end

    GetActionCooldown = function(action)
        local data = dataForAction(action)
        if data then
            local start, duration, enable = gcd(action)
            if start then return start, duration, enable end
            start, duration, enable = recharge(data)
            if start then return start, duration, enable end
        end
        return SC.nativeGetActionCooldown(action)
    end

    IsUsableAction = function(action)
        local usable, noMana = SC.nativeIsUsableAction(action)
        local data = dataForAction(action)
        if data and data.current <= 0 then return false, noMana end
        return usable, noMana
    end

    function WXL_GetSpellCharges(spellId)
        local data = SC.spells[tonumber(spellId)]
        if not data then return nil end
        local start, duration = recharge(data)
        return data.current, data.maximum, start or 0, duration or 0, 1
    end

    if not _G.GetSpellCharges then
        _G.GetSpellCharges = WXL_GetSpellCharges
    end

    if not SC.frame then SC.frame = CreateFrame("Frame") end
    SC.frame:RegisterEvent("PLAYER_LOGIN")
    SC.frame:RegisterEvent("PLAYER_ENTERING_WORLD")
    SC.frame:RegisterEvent("LEARNED_SPELL_IN_TAB")
    SC.frame:RegisterEvent("SPELLS_CHANGED")
    SC.frame:RegisterEvent("ACTIONBAR_SLOT_CHANGED")
    SC.frame:SetScript("OnEvent", function(_, event)
        syncSnapshot(false)
        updateBars()
        if event ~= "ACTIONBAR_SLOT_CHANGED" then SC.Request() end
    end)
    SC.frame:SetScript("OnUpdate", function(_, elapsed)
        SC.elapsed = (SC.elapsed or 0) + elapsed
        if SC.elapsed >= 0.1 then
            SC.elapsed = 0
            syncSnapshot(false)
            updateBars()
        end
    end)

    if type(SlashCmdList) == "table" then
        SLASH_WXLSPELLCHARGES1 = "/wxlcharges"
        SlashCmdList.WXLSPELLCHARGES = function()
            SC.Request()
            local count = 0
            for _ in pairs(SC.spells) do count = count + 1 end
            if DEFAULT_CHAT_FRAME then
                DEFAULT_CHAT_FRAME:AddMessage(
                    "WXL spell charges: " .. count ..
                    " managed spell(s), revision " .. SC.revision)
            end
        end
    end

    syncSnapshot(true)
end
)lua";

    bool Install()
    {
        bool ok = true;
        ok &= network::RegisterClientOpcode(
            opcodes::CmsgSpellChargesRequest,
            "CMSG_WXL_SPELL_CHARGES_REQUEST");
        ok &= network::RegisterServerOpcode(
            opcodes::SmsgSpellChargesUpdate,
            "SMSG_WXL_SPELL_CHARGES_UPDATE",
            &OnSnapshot);
        ok &= framescript::RegisterFunction(
            "_WXLWOW_REQUEST_SPELL_CHARGES", &LuaRequest);
        ok &= framescript::RegisterFunction(
            "_WXLWOW_SPELL_CHARGE_SNAPSHOT", &LuaSnapshotInfo);
        ok &= framescript::RegisterFunction(
            "_WXLWOW_SPELL_CHARGE_ROW", &LuaSnapshotRow);
        ok &= framescript::RegisterScript(
            "spell-charges", kBootstrap);
        return ok;
    }
}
namespace wxl_spell_charges
{
    bool InstallSpellCharges()
    {
        const bool ok = ::Install();
        if (ok)
            WLOG_INFO("server-authoritative action-bar presentation installed");
        return ok;
    }
}
