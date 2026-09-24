#pragma once

#include "CoreMinimal.h"
#include "KapsWorld/AI/KapsEnemyBase.h"
#include "KapsRangedEnemy.generated.h"

class AKapsEnemyProjectile;

UCLASS()
class SEMESTERKAPSWORLD_API AKapsRangedEnemy : public AKapsEnemyBase
{
	GENERATED_BODY()

public:
	AKapsRangedEnemy();
	
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Enemy|Attack")
	bool TryRangedAttack(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Enemy|Attack")
	bool CanAttack() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|Attack")
	float GetAttackRange() const
	{
		return AttackRange;
	}

protected:
	void ResetAttackCooldown();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat")
	TSubclassOf<AKapsEnemyProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 1200.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Movement",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float MovementSpeed = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat",
		meta = (ClampMin = "1.0"))
	float AttackDamage = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat",
		meta = (ClampMin = "1.0", Units = "cm"))
	float AttackRange = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat",
		meta = (ClampMin = "0.1", Units = "s"))
	float FireCooldown = 1.5f;

private:
	bool bAttackOnCooldown = false;
	FTimerHandle AttackCooldownTimer;
};
