// wxl-spell-charges access to the hub ABI and shared runtime services.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "common/ExtensionConfig.hpp"
#include "wxl/FrameScriptApi.h"
#include "wxl/NetworkApi.h"
#include "wxl/PluginApi.h"

namespace wxl_spell_charges
{
    extern const WXL_Api* g_api;
    extern const WXL_NetworkApi* g_network;
    extern const WXL_FrameScriptApi* g_framescript;

    inline const WXL_NetworkApi* Network()
    {
        if (!g_network)
            g_network = static_cast<const WXL_NetworkApi*>(
                g_api->GetInterface("wxl.network", WXL_NETWORK_API_VERSION));
        return g_network;
    }

    inline const WXL_FrameScriptApi* FrameScript()
    {
        if (!g_framescript)
            g_framescript = static_cast<const WXL_FrameScriptApi*>(
                g_api->GetInterface("wxl.framescript", WXL_FRAME_SCRIPT_API_VERSION));
        return g_framescript;
    }

    inline bool ConfigBool(const char* name, bool fallback)
    {
        char value[16] = {};
        return wxl::ext::config::Raw(name, value, sizeof value,
                                     "Extensions\\wxl-spell-charges\\wxl-spell-charges.cfg")
            ? wxl::ext::config::Truthy(value, fallback)
            : fallback;
    }

    bool InstallSpellCharges();
}

#define WLOG_TRACE(...) ::wxl_spell_charges::g_api->Log(WXL_LOG_TRACE, "wxl-spell-charges", __VA_ARGS__)
#define WLOG_DEBUG(...) ::wxl_spell_charges::g_api->Log(WXL_LOG_DEBUG, "wxl-spell-charges", __VA_ARGS__)
#define WLOG_INFO(...)  ::wxl_spell_charges::g_api->Log(WXL_LOG_INFO,  "wxl-spell-charges", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_spell_charges::g_api->Log(WXL_LOG_WARN,  "wxl-spell-charges", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_spell_charges::g_api->Log(WXL_LOG_ERROR, "wxl-spell-charges", __VA_ARGS__)
