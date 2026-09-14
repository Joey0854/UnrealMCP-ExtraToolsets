#pragma once

#include "CoreMinimal.h"

class FNiagaraSystemViewModel;
class UNiagaraSystem;

namespace NovaNiagara
{
	enum class EEditorLookup
	{
		/** No Niagara editor is open on the system. */
		NotOpen,
		/** OutViewModel is the open editor's view model. */
		Found,
		/** An editor is open but its view model could not be identified. */
		Unreachable,
	};

	/**
	 * Finds the view model of the Niagara system editor open on System. Tools route edits through it, or
	 * through the working copies it owns, so they work while the user keeps the editor open.
	 */
	EEditorLookup FindEditorViewModel(UNiagaraSystem& System, TSharedPtr<FNiagaraSystemViewModel>& OutViewModel);

	/** Error text for EEditorLookup::Unreachable. */
	FString UnreachableEditorMessage(const UNiagaraSystem& System);
}
