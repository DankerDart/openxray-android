////////////////////////////////////////////////////////////////////////////
//	Module 		: script_process.cpp
//	Created 	: 19.09.2003
//  Modified 	: 29.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Script process class
////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"
#include "script_engine.hpp"
#include "script_process.hpp"
#include "script_thread.hpp"
#include "Common/object_broker.h"

float ps_script_budget_ms = 2.0f;

string4096 g_ca_stdout; // XXX: allocate dynamically for each CScriptEngine instance

CScriptProcess::CScriptProcess(CScriptEngine* scriptEngine, shared_str name, shared_str scripts) : m_name(name)
{
    this->scriptEngine = scriptEngine;
#ifdef DEBUG
    Msg("* Initializing %s script process", m_name.c_str());
#endif
    string256 I;
    for (u32 i = 0, n = _GetItemCount(scripts.c_str()); i < n; i++)
        add_script(_GetItem(scripts.c_str(), i, I), false, false);
    m_iterator = 0;
}

CScriptProcess::~CScriptProcess() { delete_data(m_scripts); }
void CScriptProcess::run_scripts()
{
    while (!m_scripts_to_run.empty())
    {
        cpcstr I = m_scripts_to_run.back().m_script_name;
        const bool do_string = m_scripts_to_run.back().m_do_string;
        const bool reload = m_scripts_to_run.back().m_reload;
        pstr S = xr_strdup(I);
        m_scripts_to_run.pop_back();

        CScriptThread* script = scriptEngine->CreateScriptThread(S, do_string, reload);
        xr_free(S);

        if (script && script->active())
            m_scripts.push_back(script);
        else
            xr_delete(script);
    }
}

// Oles:
// changed log-output to stack-based buffer (avoid persistent 4K storage)
//
// Script updates run round-robin under a wall-clock budget rather than at a
// fixed count per frame. The upstream engine updated every script in the
// process every frame, which is unbounded on a phone -- a level with a few
// dozen scripts can spend more than a whole frame in the VM. Throttling the
// other way (one script per frame) starves them instead: with N scripts each
// one only advances once every N frames, so at 4 fps a trigger, timer or NPC
// script can sit unrun for ten seconds and the game looks frozen. Sweeping
// the list under a budget keeps per-frame work bounded *and* keeps every
// script serviced on every frame that can afford it.
void CScriptProcess::update()
{
#ifdef DBG_DISABLE_SCRIPTS
    m_scripts_to_run.clear();
    return;
#else
    run_scripts();
    if (m_scripts.empty())
        return;
    // update script
    g_ca_stdout[0] = 0;

    const u64 deadline = CPU::QPC() + (u64)(ps_script_budget_ms * CPU::qpc_freq / 1000.0);
    // Service every script that was alive when the frame started, resuming from
    // wherever the previous frame stopped so the sweep stays fair. When a
    // script dies it is erased in place and the next one shifts down into that
    // slot, so leave the cursor on it rather than advancing.
    u32 remaining = m_scripts.size();
    u32 id = m_iterator;
    while (remaining-- != 0 && !m_scripts.empty())
    {
        if (id >= m_scripts.size())
            id = 0;
        if (!m_scripts[id]->update())
        {
            xr_delete(m_scripts[id]);
            m_scripts.erase(m_scripts.begin() + id);
        }
        else
        {
            ++id;
        }
        // Always service at least one script, then stop once this frame's share
        // of the script VM is spent.
        if (CPU::QPC() >= deadline)
            break;
    }
    m_iterator = m_scripts.empty() ? 0 : (id % m_scripts.size());

    if (g_ca_stdout[0])
    {
        fputc(0, stderr);
        scriptEngine->script_log(LuaMessageType::Info, "%s", g_ca_stdout);
        fflush(stderr);
    }
#if defined(DEBUG)
    try
    {
#pragma todo("Dima cant find this function 'lua_setgcthreshold' ")
        lua_gc(scriptEngine->lua(), LUA_GCSTEP, 0);
    }
    catch (...)
    {
    }
#endif
#endif
}

void CScriptProcess::add_script(LPCSTR script_name, bool do_string, bool reload)
{
    m_scripts_to_run.emplace_back(script_name, do_string, reload);
}
