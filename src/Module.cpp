#include "ExtensionApi.hpp"

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    static const WXL_PluginInfo info{
        sizeof(WXL_PluginInfo), WXL_API_VERSION, "wxl-spell-charges", 1, WXL_CLIENT_BUILD,
    };
    return &info;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;
    wxl_spell_charges::g_api = api;

    if (!wxl_spell_charges::Network())
    {
        api->Log(WXL_LOG_ERROR, "wxl-spell-charges", "required wxl.network v1 is unavailable");
        return 0;
    }
    if (!wxl_spell_charges::FrameScript())
    {
        api->Log(WXL_LOG_ERROR, "wxl-spell-charges", "required wxl.framescript v1 is unavailable");
        return 0;
    }
    return wxl_spell_charges::InstallSpellCharges() ? 1 : 0;
}
