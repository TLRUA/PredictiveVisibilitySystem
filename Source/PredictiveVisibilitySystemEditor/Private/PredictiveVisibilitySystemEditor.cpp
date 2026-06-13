#include "PredictiveVisibilitySystemEditor.h"

#include "Editor.h"
#include "PredictiveVisibilityEditorSubsystem.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "PredictiveVisibilitySystemEditor"

void FPredictiveVisibilitySystemEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPredictiveVisibilitySystemEditorModule::RegisterMenus));
}

void FPredictiveVisibilitySystemEditorModule::ShutdownModule()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
}

void FPredictiveVisibilitySystemEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!ToolsMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("PredictiveVisibility");
	Section.Label = LOCTEXT("PredictiveVisibilitySection", "Predictive Visibility");
	Section.AddMenuEntry(
		"PredictiveVisibilityBakeCurrentWorld",
		LOCTEXT("PredictiveVisibilityBakeCurrentWorld", "Bake Current World"),
		LOCTEXT("PredictiveVisibilityBakeCurrentWorldTooltip", "Bake predictive visibility data for the current editor world."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (!GEditor)
			{
				return;
			}

			if (UPredictiveVisibilityEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPredictiveVisibilityEditorSubsystem>())
			{
				Subsystem->BakeCurrentWorld();
			}
		})));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPredictiveVisibilitySystemEditorModule, PredictiveVisibilitySystemEditor)
