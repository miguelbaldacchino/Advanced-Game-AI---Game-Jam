#include "UMetaTailorSkeletalMeshComponent.h"

UMetaTailorSkeletalMeshComponent::UMetaTailorSkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMetaTailorSkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();	
}

void UMetaTailorSkeletalMeshComponent::OnRegister()
{
	Super::OnRegister();
	this->SetLeaderPoseComponent(GetOwner()->FindComponentByClass<USkeletalMeshComponent>());
}

void UMetaTailorSkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                             FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

