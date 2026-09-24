#include "KapsWorld/Weapons/KapsWeaponBase.h"

#include "Engine/World.h"

AKapsWeaponBase::AKapsWeaponBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AKapsWeaponBase::TryPrimaryAttack()
{
	if (!IsPrimaryAttackReady() || !CanPerformPrimaryAttack())
	{
		return false;
	}

	LastAttackTime = GetWorld()->GetTimeSeconds();
	PerformPrimaryAttack();

	return true;
}

bool AKapsWeaponBase::IsPrimaryAttackReady() const
{
	const UWorld* World = GetWorld();

	if (!World)
	{
		return false;
	}

	return World->GetTimeSeconds() - LastAttackTime >= AttackCooldown;
}

bool AKapsWeaponBase::CanPerformPrimaryAttack_Implementation() const
{
	return true;
}