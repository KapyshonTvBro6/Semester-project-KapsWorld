#include "KapsWorld/AI/KapsRangedEnemy.h"
#include "KapsWorld/AI/KapsEnemyProjectile.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AKapsRangedEnemy::AKapsRangedEnemy()
{
	GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;
	
	ProjectileClass = AKapsEnemyProjectile::StaticClass();
}

void AKapsRangedEnemy::BeginPlay()
{
	Super::BeginPlay();

	GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;
}

bool AKapsRangedEnemy::CanAttack() const
{
	return !IsDead() && !bAttackOnCooldown;
}

bool AKapsRangedEnemy::TryRangedAttack(AActor* Target)
{
	if (!CanAttack() || !IsValid(Target))
	{
		return false;
	}

	const float DistanceToTarget = FVector::Dist2D(
		GetActorLocation(),
		Target->GetActorLocation()
	);

	if (DistanceToTarget > AttackRange)
	{
		return false;
	}

	if (!IsValid(ProjectileClass))
	{
		return false;
	}

	const FVector SpawnLocation =
		GetActorLocation() +
		GetActorForwardVector() * 100.0f +
		FVector(0.0f, 0.0f, 50.0f);

	const FVector TargetLocation =
		Target->GetActorLocation() +
		FVector(0.0f, 0.0f, 50.0f);

	const FVector ShotDirection =
		(TargetLocation - SpawnLocation).GetSafeNormal();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;

	AKapsEnemyProjectile* Projectile =
		GetWorld()->SpawnActor<AKapsEnemyProjectile>(
			ProjectileClass,
			SpawnLocation,
			ShotDirection.Rotation(),
			SpawnParameters
		);

	if (!IsValid(Projectile))
	{
		return false;
	}

	Projectile->InitializeProjectile(
		AttackDamage,
		ProjectileSpeed,
		GetController()
	);

	bAttackOnCooldown = true;

	GetWorldTimerManager().SetTimer(
		AttackCooldownTimer,
		this,
		&AKapsRangedEnemy::ResetAttackCooldown,
		FireCooldown,
		false
	);

	return true;
}

void AKapsRangedEnemy::ResetAttackCooldown()
{
	bAttackOnCooldown = false;
}
