#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "UMetaTailorSkeletalMeshComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class METATAILORBRIDGERUNTIME_API UMetaTailorSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UMetaTailorSkeletalMeshComponent();

protected:
	virtual void BeginPlay() override;
	virtual void OnRegister() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
