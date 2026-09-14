#include "NovaNiagaraEditorViewModel.h"

#include "Editor.h"
#include "NiagaraEditorModule.h"
#include "NiagaraSystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ViewModels/NiagaraSystemViewModel.h"

namespace NovaNiagara
{
	EEditorLookup FindEditorViewModel(UNiagaraSystem& System, TSharedPtr<FNiagaraSystemViewModel>& OutViewModel)
	{
		OutViewModel.Reset();
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (AssetEditorSubsystem == nullptr || AssetEditorSubsystem->FindEditorsForAsset(&System).Num() == 0)
		{
			return EEditorLookup::NotOpen;
		}

		// Every system view model registers itself for its system, headless ones included, and the exported
		// lookup returns the oldest registration. Headless models are created for a single edit and destroyed
		// right after, so while the editor is open the oldest one is the editor's. Only non-headless models
		// create a preview component, which confirms it.
		TSharedPtr<FNiagaraSystemViewModel> ViewModel = FNiagaraEditorModule::Get().GetExistingViewModelForSystem(&System);
		if (!ViewModel.IsValid() || ViewModel->GetPreviewComponent() == nullptr)
		{
			return EEditorLookup::Unreachable;
		}
		OutViewModel = ViewModel;
		return EEditorLookup::Found;
	}

	FString UnreachableEditorMessage(const UNiagaraSystem& System)
	{
		return FString::Printf(TEXT("The Niagara editor for '%s' is open but its view model could not be found. Close and reopen that editor, then retry."), *System.GetPathName());
	}
}
