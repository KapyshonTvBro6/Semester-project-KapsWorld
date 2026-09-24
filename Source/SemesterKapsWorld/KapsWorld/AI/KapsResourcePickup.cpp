#include "KapsWorld/AI/KapsResourcePickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

AKapsResourcePickup::AKapsResourcePickup()
{
	PrimaryActorTick.bCanEverTick = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(55.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Overlap);
	RootComponent = Collision;

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(Collision);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetRelativeScale3D(FVector(0.25f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube")
	);

	if (CubeMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(CubeMesh.Object);
	}
}

void AKapsResourcePickup::InitializePickup(FName InResourceId, int32 InAmount)
{
	ResourceId = InResourceId;
	Amount = FMath::Max(1, InAmount);
}

void AKapsResourcePickup::NotifyActorBeginOverlap(AActor* OtherActor)
{
	APawn* Pawn = Cast<APawn>(OtherActor);

	if (IsValid(Pawn) && Pawn->IsPlayerControlled())
	{
		OnResourceCollected.Broadcast(ResourceId, Amount);
		Destroy();
	}
}
