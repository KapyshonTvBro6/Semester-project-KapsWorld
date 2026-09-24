// KapsEnemyBase.cpp
#include "KapsWorld/AI/KapsEnemyBase.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KapsWorld/AI/KapsResourcePickup.h"

AKapsEnemyBase::AKapsEnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AKapsEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;
}

float AKapsEnemyBase::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser
)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	const float AppliedDamage = Super::TakeDamage(
		DamageAmount,
		DamageEvent,
		EventInstigator,
		DamageCauser
	);

	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	const float PreviousHealth = CurrentHealth;

	CurrentHealth = FMath::Clamp(
		CurrentHealth - AppliedDamage,
		0.0f,
		MaxHealth
	);

	OnHealthChanged.Broadcast(
		CurrentHealth,
		CurrentHealth - PreviousHealth
	);

	if (CurrentHealth <= 0.0f)
	{
		Die();
	}

	return AppliedDamage;
}

bool AKapsEnemyBase::IsDead() const
{
	return bIsDead;
}

float AKapsEnemyBase::GetHealthPercent() const
{
	return MaxHealth > 0.0f ? CurrentHealth / MaxHealth : 0.0f;
}

void AKapsEnemyBase::Die()
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;

	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		AIController->StopMovement();
	}

	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SpawnResourceDrops();

	OnEnemyDied.Broadcast(this);
}

void AKapsEnemyBase::SpawnResourceDrops()
{
	for (const FKapsResourceDrop& Drop : ResourceDrops)
	{
		if (Drop.ResourceId.IsNone() ||
			FMath::FRandRange(0.0f, 100.0f) > Drop.DropChance)
		{
			continue;
		}

		const int32 MaxAmount = FMath::Max(Drop.MinAmount, Drop.MaxAmount);
		const int32 Amount = FMath::RandRange(Drop.MinAmount, MaxAmount);

		const FVector Offset = FMath::VRand() * LootScatterRadius;
		const FVector SpawnLocation = GetActorLocation() +
			FVector(Offset.X, Offset.Y, 20.0f);

		TSubclassOf<AKapsResourcePickup> PickupClass = Drop.PickupClass;

		if (!PickupClass)
		{
			PickupClass = AKapsResourcePickup::StaticClass();
		}

		AKapsResourcePickup* Pickup = GetWorld()->SpawnActor<AKapsResourcePickup>(
			PickupClass,
			SpawnLocation,
			FRotator::ZeroRotator
		);

		if (IsValid(Pickup))
		{
			Pickup->InitializePickup(Drop.ResourceId, Amount);
		}
	}
}
