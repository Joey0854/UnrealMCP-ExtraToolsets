#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"

#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "NovaToolset_Blueprint.h"
#include "NovaToolset_Niagara.h"
#include "NovaToolset_NiagaraGraph.h"
#include "NovaToolset_NiagaraUserParams.h"
#include "NovaToolset_RenderTarget.h"

// Registration mirrors NiagaraToolsetsModule.cpp: try at startup, retry once every module has
// loaded (the registry subsystem may not exist yet at PostEngineInit), unregister before exit.
class FNovaToolsetsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FNovaToolsetsModule::RegisterToolsets);
		FCoreDelegates::OnPreExit.AddRaw(this, &FNovaToolsetsModule::UnregisterToolsets);
		RegisterToolsets();
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnAllModuleLoadingPhasesComplete.RemoveAll(this);
		FCoreDelegates::OnPreExit.RemoveAll(this);
		UnregisterToolsets();
	}

private:
	void RegisterToolsets()
	{
		if (bToolsetsRegistered)
		{
			return;
		}

		if (UToolsetRegistrySubsystem::Get().HasError())
		{
			return;
		}

		bToolsetsRegistered = true;
		UToolsetRegistry::RegisterToolsetClass(UNovaToolset_Niagara::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UNovaToolset_NiagaraGraph::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UNovaToolset_RenderTarget::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UNovaToolset_Blueprint::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UNovaToolset_NiagaraUserParams::StaticClass());
	}

	void UnregisterToolsets()
	{
		if (!bToolsetsRegistered)
		{
			return;
		}

		bToolsetsRegistered = false;
		UToolsetRegistry::UnregisterToolsetClass(UNovaToolset_Niagara::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UNovaToolset_NiagaraGraph::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UNovaToolset_RenderTarget::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UNovaToolset_Blueprint::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UNovaToolset_NiagaraUserParams::StaticClass());
	}

	bool bToolsetsRegistered = false;
};

IMPLEMENT_MODULE(FNovaToolsetsModule, NovaToolsets)
