-- Minimal gm_mcp contract host (not Dreamwork).
-- Use only for local/CI smoke against the native poll/respond API.
-- Production host: https://github.com/Pika-Software/dreamwork (modules/mcp.lua)

if not SERVER then return end

require("mcp")

mcp.setStatus(true, "minimal contract host")

timer.Create("gm_mcp.minimal_host", 0.1, 0, function()
    local command = mcp.pollCommand()
    if not command then return end

    local id = command.requestId
    local tool = command.tool or "exec_lua_code"

    if tool == "gmod_status" then
        return
    end

    if not id or id == "" then
        return
    end

    if tool == "screenshot" then
        mcp.respondScreenshot(id, false, "minimal host has no client capture")
        return
    end

    if tool == "gmod_players" then
        mcp.respondLua(id, true, util.TableToJSON({ value = {} }))
        return
    end

    if tool == "gmod_entities" then
        mcp.respondLua(id, true, util.TableToJSON({
            value = { entities = {}, truncated = false },
        }))
        return
    end

    -- exec_lua_code / unknown waiting tools
    local ok, result = pcall(CompileString(command.code or "", "gm_mcp.minimal_host", false))
    if not ok then
        mcp.respondLua(id, false, tostring(result))
        return
    end
    if isfunction(result) then
        ok, result = pcall(result)
    end
    if command.returnResult then
        mcp.respondLua(id, ok, util.TableToJSON({ value = result }))
    elseif not ok then
        mcp.respondError(id, tostring(result))
    end
end)
