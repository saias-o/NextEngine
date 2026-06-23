#pragma once

// MCP protocol + tool surface for NextEngine. Speaks JSON-RPC MCP
// (initialize / tools/list / tools/call) over the McpServer transport and turns
// tool calls into reflection-validated, undoable scene edits routed through the
// editor's command history. This is "the hands of the LLM".
//
// Editor/dev only (ne_editor, NE_ENABLE_MCP). Friend of EditorUI so tools reach
// the document + command chokepoint without widening EditorUI's public API.

#include "nlohmann/json_fwd.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ne {

class EditorUI;
class McpServer;

class McpBridge {
public:
    McpBridge();
    ~McpBridge();
    McpBridge(const McpBridge&) = delete;
    McpBridge& operator=(const McpBridge&) = delete;

    bool start(uint16_t port);
    bool running() const;

    // Drain + dispatch any queued MCP requests on the main thread. The editor
    // calls this once per frame while `ui` is fully bound (ctx pointers set).
    void poll(EditorUI& ui);

private:
    nlohmann::json dispatch(EditorUI& ui, const std::string& method, const nlohmann::json& params);
    nlohmann::json callTool(EditorUI& ui, const std::string& name, const nlohmann::json& args);

    std::unique_ptr<McpServer> server_;
};

} // namespace ne
