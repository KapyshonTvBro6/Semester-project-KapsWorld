#include "KapsWorld/AI/KapsBossEnemy.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "KapsWorld/AI/KapsEnemyProjectile.h"
#include "TimerManager.h"

AKapsBossEnemy::AKapsBossEnemy()
{
	GetCharacterMovement()->MaxWalkSpeed = PhaseOneMoveSpeed;
	ProjectileClass = AKapsEnemyProjectile::StaticClass();
	MaxHealth = 500.0f;
}

void AKapsBossEnemy::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = PhaseOneMoveSpeed;
}

float AKapsBossEnemy::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser
)
{
	const float AppliedDamage = Super::TakeDamage(
		DamageAmount,
		DamageEvent,
		EventInstigator,
		DamageCauser
	);

	if (!bIsPhaseTwo && !IsDead() &&
		GetHealthPercent() <= PhaseTwoHealthPercent)
	{
		EnterPhaseTwo();
	}

	return AppliedDamage;
}

bool AKapsBossEnemy::CanAttack() const
{
	return !IsDead() && !bAttackOnCooldown;
}

bool AKapsBossEnemy::TryRangedAttack(AActor* Target)
{
	if (!CanAttack() || !IsValid(Target) || !ProjectileClass)
	{
		return false;
	}

	const int32 ProjectileCount = bIsPhaseTwo ? PhaseTwoProjectileCount : 1;
	const float Spread = bIsPhaseTwo ? PhaseTwoSpreadDegrees : 0.0f;
	const FVector SpawnLocation = GetActorLocation() + FVector(0.0f, 0.0f, 70.0f);
	const FVector TargetLocation = Target->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
	const FVector BaseDirection = (TargetLocation - SpawnLocation).GetSafeNormal();

	for (int32 Index = 0; Index < ProjectileCount; ++Index)
	{
		const float Alpha = ProjectileCount == 1 ? 0.5f :
			static_cast<float>(Index) / static_cast<float>(ProjectileCount - 1);

		const float Angle = FMath::Lerp(-Spread * 0.5f, Spread * 0.5f, Alpha);
		const FVector Direction = BaseDirection.RotateAngleAxis(Angle, FVector::UpVector);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.Instigator = this;

		AKapsEnemyProjectile* Projectile = GetWorld()->SpawnActor<AKapsEnemyProjectile>(
			ProjectileClass,
			SpawnLocation + Direction * 100.0f,
			Direction.Rotation(),
			SpawnParameters
		);

		if (IsValid(Projectile))
		{
			Projectile->InitializeProjectile(
				ProjectileDamage,
				ProjectileSpeed,
				GetController()
			);
		}
	}

	bAttackOnCooldown = true;

	GetWorldTimerManager().SetTimer(
		AttackCooldownTimer,
		this,
		&AKapsBossEnemy::ResetAttackCooldown,
		bIsPhaseTwo ? PhaseTwoAttackCooldown : PhaseOneAttackCooldown,
		false
	);

	return true;
}

void AKapsBossEnemy::EnterPhaseTwo()
{
	bIsPhaseTwo = true;
	GetCharacterMovement()->MaxWalkSpeed = PhaseTwoMoveSpeed;
}

void AKapsBossEnemy::ResetAttackCooldown()
{
	bAttackOnCooldown = false;
}
