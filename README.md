# UnrealMCP-ExtraToolsets

Extra toolsets for **Unreal MCP**, the experimental Model Context Protocol server in Unreal Engine 5.8. They cover editor work the stock toolsets can't do, so an AI agent no longer needs you to click through it by hand:

- **Niagara**: add, reorder and configure simulation stages and event handlers, place modules in their stacks, create local (scratch pad) modules, set user parameter descriptions, organise the User Parameters panel into sections and categories, and find where each user parameter is referenced.
- **Niagara node graphs**: read and edit module graphs, including Custom HLSL pins and data interface function specifiers such as `Attribute`.
- **Blueprints**: set variable metadata (tooltips, clamp and slider ranges) and function metadata (Call In Editor), add or remove implemented interfaces, and dump a graph as compact text to check what was built.
- **Render targets**: create and resize 2D and volume render targets without the modal dialogs that stall the game thread.

The plugin folder is named `NovaToolsets` after the project it was written for. It does not depend on that project and ships no content assets.

> **Status: experimental.** It was built and used on a UE 5.8 source build. [Tool status](#tool-status) lists which tools have been run in the editor.

## Why

Driving the editor through MCP with the stock toolsets runs into these problems:

- You can't create simulation stages, event handlers or local modules. Writing the `SimulationStages` array through `SetEmitterData` crashes the editor.
- Resizing a render target through `set_properties` opens a modal confirmation dialog on the game thread. Every later MCP call then hangs.
- Data interface function nodes added from code get an empty `Attribute` specifier. A Particle Attribute Reader node then returns zeros without reporting an error.
- The Blueprint graph text tools match against localized display strings. In a non-English editor there was no reliable way to check a graph after building it.

These tools call the same editor code the UI does. They avoid modal dialogs, and when something goes wrong they raise an error.

## Requirements

- Unreal Engine **5.8** on Windows. Other platforms are untested. The plugin is editor-only.
- These engine plugins must be enabled in your project:
  - **Model Context Protocol** (`ModelContextProtocol`)
  - **Toolset Registry** (`ToolsetRegistry`)
  - **Niagara**
- A project that can compile C++. The plugin ships as source only.

## Installation

1. Clone or copy this repository into your project's `Plugins` folder. Any folder name works; this example uses `Plugins/NovaToolsets`.
   ```
   git clone https://github.com/Joey0854/UnrealMCP-ExtraToolsets.git Plugins/NovaToolsets
   ```
2. Close the editor and build the editor target:
   ```
   Engine\Build\BatchFiles\Build.bat <YourProject>Editor Win64 Development -Project="<path>\<YourProject>.uproject" -WaitMutex
   ```
3. Start the editor. The toolsets register with the Toolset Registry after all modules have loaded.

If you add a tool or change a tool's C++ signature, restart the editor. The MCP client may also need to reconnect.

## Usage

The Unreal MCP server exposes toolsets through three gateway tools: `list_toolsets`, `describe_toolset` and `call_tool`. This plugin adds these toolsets:

| Toolset | Scope |
|---|---|
| `NovaToolsets.NovaToolset_Niagara` | Emitter stages, event handlers, stage modules, local modules, user parameter descriptions |
| `NovaToolsets.NovaToolset_NiagaraUserParams` | User Parameters panel layout (sections and categories) and parameter reference scan |
| `NovaToolsets.NovaToolset_NiagaraGraph` | Node graphs of local modules and module assets |
| `NovaToolsets.NovaToolset_Blueprint` | Blueprint variable and function metadata, graph dump |
| `NovaToolsets.NovaToolset_RenderTarget` | Creating and resizing 2D and volume render targets |

Parameter names in the generated schema are camelCase, as with other Toolset Registry tools. Call `describe_toolset` for the exact schema. Example:

```json
{
  "toolset_name": "NovaToolsets.NovaToolset_NiagaraGraph",
  "tool_name": "SetFunctionSpecifier",
  "arguments": {
    "script": "/Game/FX/NS_Example.NS_Example:MyLocalModule",
    "nodeId": "<NodeId from GetGraph>",
    "specifier": "Attribute",
    "value": "Position"
  }
}
```

### Tools

**`NovaToolset_Niagara`**

| Tool | What it does |
|---|---|
| `GetEmitterStages` | Lists an emitter's simulation stages and event handlers with their settings |
| `AddSimulationStage` | Adds a simulation stage through the emitter's "+ Stage" menu at any position, and sets its iteration source and data interface binding |
| `MoveSimulationStage` | Moves a stage to a new position, like dragging it in the stack |
| `AddEventHandler` | Adds an event handler (CPU emitters only) and sets its source and spawn settings |
| `AddModuleToStage` | Places a module in a simulation stage or event handler stack |
| `CreateLocalModule` | Creates a local (scratch pad) module and places it in any stack |
| `ListLocalModules` | Lists a system's local modules |
| `SetUserVariableDescription` | Sets a user parameter's description, including parameters of data interface types |

**`NovaToolset_NiagaraUserParams`**

| Tool | What it does |
|---|---|
| `GetUserParameterHierarchy` | Reads the section (tab) and category layout of the User Parameters panel |
| `SetUserParameterHierarchy` | Replaces that layout, which is also shown in the Details panel of placed components |
| `GetUserParameterUsage` | Lists where each user parameter is still referenced, to check before removing one with the stock `NiagaraToolset_System.RemoveUserVariables` |

**`NovaToolset_NiagaraGraph`**

| Tool | What it does |
|---|---|
| `GetGraph` | Returns the graph's nodes, pins, types, default values, connections, Custom HLSL code and specifiers |
| `ListNodeActions` | Lists the node creation menu entries, optionally as if dragging from a pin |
| `AddNode` | Creates a node from a menu entry |
| `AddParameterPin` | Adds a named parameter pin to a Map Get or Map Set node |
| `SetFunctionSpecifier` | Sets a specifier on a data interface function node, e.g. `Attribute` on *Get Position by Index* |
| `ConnectPins` / `DisconnectPins` | Connects or disconnects pins, with the same type checks as the editor |
| `RemoveNode` | Deletes a node |
| `SetPinDefaultValue` | Sets the inline value of an unconnected input pin |
| `SetCustomHlsl` | Sets a Custom HLSL node's code, include files, and input/output pins |
| `ApplyGraphChanges` | Applies graph edits to every module instance (like the local module editor's Apply button), then recompiles and saves |

**`NovaToolset_Blueprint`**

| Tool | What it does |
|---|---|
| `SetVariableMetadata` | Sets or removes `Tooltip`, `ClampMin`/`ClampMax`, `UIMin`/`UIMax` or other metadata on a member variable |
| `SetFunctionMetadata` | Sets `CallInEditor`, `Tooltip`, `Category` or `Keywords` on a function. On custom events only `CallInEditor` is supported |
| `DumpGraph` | Dumps a graph as one line per node, followed by its connections and set values; the output is the same in any editor language |
| `ListInterfaces` | Lists the interfaces a Blueprint implements and the function graphs they created |
| `AddInterface` | Adds a native or Blueprint interface (Class Settings > Implemented Interfaces > Add), with conflicts reported before anything is created |
| `RemoveInterface` | Removes an interface, optionally keeping its function graphs as ordinary functions |

**`NovaToolset_RenderTarget`**

| Tool | What it does |
|---|---|
| `CreateRenderTarget2D` | Creates a 2D render target with its size and format set at creation |
| `CreateRenderTargetVolume` | Creates a volume (3D) render target |
| `ResizeRenderTarget` | Resizes an existing render target without going through property editing, so no dialog opens |
| `GetRenderTargetInfo` | Reads size, pixel format and UAV support |

## Behavior notes

- **Works with the Niagara editor open.**
  - Stage menu actions run on the open editor's own view model, and the editor refreshes after each change.
  - Graph tools read and edit the open editor's working copy of a local module. `ApplyGraphChanges` then clicks that editor's Apply button, which also applies any unapplied edits the user made there.
  - Before using write tools on a module asset or an emitter asset, close that asset's own editor.
- **Undo:** each write tool runs inside an editor transaction.
- **No modal dialogs:**
  - Saving calls `UPackage::SavePackage` directly.
  - Render target sizes are set without calling `PostEditChangeProperty`.
  - Node creation checks `CanAddToGraph` first; otherwise the engine would open a message box.
- **Errors:** failures are raised as script errors that list the valid choices, such as existing emitter or variable names.
- **GPU shader errors** from Custom HLSL do not appear in the system compile status. Look in the output log for `LogShaderCompilers` and `LogNiagara` warnings.

## Tool status

Everything in this repository compiles against UE 5.8. Not every tool has been run in the editor yet:

| Tools | Status |
|---|---|
| `NovaToolset_Niagara`: stages, event handlers, stage modules, local modules; all `NovaToolset_NiagaraGraph` tools; all `NovaToolset_RenderTarget` tools | Run in the editor on real systems, including with the Niagara editor open |
| `ListInterfaces`, `AddInterface` (including its error paths); `GetUserParameterUsage`, `SetUserParameterHierarchy` | Run in the editor |
| `RemoveInterface`, `SetVariableMetadata`, `SetFunctionMetadata`, `DumpGraph`, `SetUserVariableDescription`, `GetUserParameterHierarchy` | Compiled and code-reviewed, **not yet run** |

Issues and pull requests are welcome.

## License

[MIT](LICENSE). This project is not affiliated with or endorsed by Epic Games. Unreal and Unreal Engine are trademarks or registered trademarks of Epic Games, Inc.
