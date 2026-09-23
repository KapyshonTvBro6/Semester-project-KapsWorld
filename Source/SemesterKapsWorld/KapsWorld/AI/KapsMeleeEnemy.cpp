// KapsMeleeEnemy.cpp
#include "KapsWorld/AI/KapsMeleeEnemy.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AKapsMeleeEnemy::AKapsMeleeEnemy()
{
	GetCharacterMovement()->MaxWalkSpeed = 260.0f;
}

bool AKapsMeleeEnemy::CanAttack() const
{
	return !IsDead() && !bAttackOnCooldown;
}

bool AKapsMeleeEnemy::TryAttack(AActor* Target)
{
	if (!CanAttack() || !IsValid(Target))
	{
		return false;
	}

	const float DistanceToTarget = FVector::Dist(
		GetActorLocation(),
		Target->GetActorLocation()
	);

	if (DistanceToTarget > AttackRange)
	{
		return false;
	}

	UGameplayStatics::ApplyDamage(
		Target,
		AttackDamage,
		GetController(),
		this,
		nullptr
	);

	bAttackOnCooldown = true;

	GetWorldTimerManager().SetTimer(
		AttackCooldownTimer,
		this,
		&AKapsMeleeEnemy::ResetAttackCooldown,
		AttackCooldown,
		false
	);

	return true;
}

void AKapsMeleeEnemy::ResetAttackCooldown()
{
	bAttackOnCooldown = false;
}
