# MatBP2FP UE 5.8 MCP

This optional Editor plugin exposes the MatBP2FP material bridge through Unreal Engine 5.8's native MCP ToolsetRegistry.

## Compatibility

The module is isolated by the MATBP2FP_WITH_UE58_MCP macro. UE 4.x and UE 5.0-5.7 build the module without including ToolsetRegistry or ModelContextProtocol headers. UE 5.8 and newer register the two toolsets during module startup.

## Enablement

1. Enable this plugin in the project.
2. Enable the engine plugins ToolsetRegistry and ModelContextProtocol.
3. Start the editor MCP server with -ModelContextProtocolStartServer, or enable it in Model Context Protocol editor settings.
4. Keep Tool Search enabled for the default list_toolsets, describe_toolset, and call_tool discovery flow.

The default MCP endpoint is http://localhost:8000/mcp.

## Exposed toolsets

- MatBP2FPInspectToolset: export, round-trip validation, mapping lookup, and DSL path conversion.
- MatBP2FPEditToolset: text import and text update. Package saving is opt-in and defaults to false.

All material paths are restricted to normalized /Game paths. File-based and arbitrary Python execution are intentionally not exposed by this native surface.
