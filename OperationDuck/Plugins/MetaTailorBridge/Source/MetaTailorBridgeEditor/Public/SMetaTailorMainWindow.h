#pragma once

#include "CoreMinimal.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SButton;
class STextBlock;
class SCheckBox;
class SVerticalBox;

class FMetaTailorIPC; 
class FMetaTailorAssetHandler;

class SMetaTailorMainWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMetaTailorMainWindow) {}
    SLATE_END_ARGS()

    bool IsDockedInBlueprint();
    void Construct(const FArguments& InArgs);

    virtual ~SMetaTailorMainWindow() override;
    FAssetPickerConfig CreateAssetPickerConfiguration(FString OutfitPath);
    
private:
    TSharedPtr<SCheckBox> ReplaceToggle;
    TSharedPtr<SCheckBox> LimitCompatToggle;

    FReply OnSettingsClicked();
    FReply OnHelpClicked();

    void OnReplaceToggleChanged(ECheckBoxState NewState);
    void OnLimitCompatToggleChanged(ECheckBoxState NewState);
    
    bool bReplaceClothing = false;
    bool bLimitBrowserToCompatible = false;
    
    void UpdateWidgetStates();
};