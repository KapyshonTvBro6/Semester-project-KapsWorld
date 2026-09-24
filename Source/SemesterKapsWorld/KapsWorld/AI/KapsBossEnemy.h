#pragma once

#include "CoreMinimal.h"
#include "KapsWorld/AI/KapsEnemyBase.h"
#include "KapsBossEnemy.generated.h"

class AKapsEnemyProjectile;

UCLASS()
class SEMESTERKAPSWORLD_API AKapsBossEnemy : public AKapsEnemyBase
{
	GENERATED_BODY()

public:
	AKapsBossEnemy();

	virtual void BeginPlay() override;

	virtual float TakeDamage(
		float DamageAmount,
		FDamageEvent const& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser
	) override;

	UFUNCTION(BlueprintCallable, Category = "Boss|Attack")
	bool TryRangedAttack(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Boss|Attack")
	bool CanAttack() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	bool IsPhaseTwo() const
	{
		return bIsPhaseTwo;
	}

	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	float GetPreferredMinRange() const
	{
		return PreferredMinRange;
	}

	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	float GetPreferredMaxRange() const
	{
		return PreferredMaxRange;
	}

protected:
	void EnterPhaseTwo();
	void ResetAttackCooldown();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Movement",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float PhaseOneMoveSpeed = 210.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Movement",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float PhaseTwoMoveSpeed = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Combat",
		meta = (ClampMin = "1.0", Units = "cm"))
	float PreferredMinRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Combat",
		meta = (ClampMin = "1.0", Units = "cm"))
	float PreferredMaxRange = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Combat")
	TSubclassOf<AKapsEnemyProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Combat",
		meta = (ClampMin = "1.0"))
	float ProjectileDamage = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Combat",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Combat",
		meta = (ClampMin = "0.1", Units = "s"))
	float PhaseOneAttackCooldown = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase",
		meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float PhaseTwoHealthPercent = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase",
		meta = (ClampMin = "0.1", Units = "s"))
	float PhaseTwoAttackCooldown = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase",
		meta = (ClampMin = "1", ClampMax = "9"))
	int32 PhaseTwoProjectileCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase",
		meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float PhaseTwoSpreadDegrees = 55.0f;

private:
	bool bIsPhaseTwo = false;
	bool bAttackOnCooldown = false;
	FTimerHandle AttackCooldownTimer;
};
