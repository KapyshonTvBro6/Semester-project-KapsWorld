// KapsMeleeEnemy.h
#pragma once

#include "CoreMinimal.h"
#include "KapsWorld/AI/KapsEnemyBase.h"
#include "KapsMeleeEnemy.generated.h"

UCLASS()
class SEMESTERKAPSWORLD_API AKapsMeleeEnemy : public AKapsEnemyBase
{
	GENERATED_BODY()

public:
	AKapsMeleeEnemy();

	UFUNCTION(BlueprintCallable, Category = "Enemy|Attack")
	bool TryAttack(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Enemy|Attack")
	bool CanAttack() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|Attack")
	float GetAttackRange() const
	{
		return AttackRange;
	}
	
protected:
	void ResetAttackCooldown();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Attack",
		meta = (ClampMin = "1.0"))
	float AttackDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Attack",
		meta = (ClampMin = "1.0", Units = "cm"))
	float AttackRange = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Attack",
		meta = (ClampMin = "0.1", Units = "s"))
	float AttackCooldown = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Attack")
	bool bAttackOnCooldown = false;

	FTimerHandle AttackCooldownTimer;
};