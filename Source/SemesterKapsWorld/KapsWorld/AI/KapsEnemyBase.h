// KapsEnemyBase.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KapsWorld/AI/KapsResourceTypes.h"
#include "KapsEnemyBase.generated.h"

class AController;
struct FDamageEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnEnemyHealthChanged,
	float,
	NewHealth,
	float,
	HealthDelta
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnEnemyDied,
	AActor*,
	Enemy
);

UCLASS()
class SEMESTERKAPSWORLD_API AKapsEnemyBase : public ACharacter
{
	GENERATED_BODY()

public:
	AKapsEnemyBase();

	virtual float TakeDamage(
		float DamageAmount,
		FDamageEvent const& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser
	) override;

	UFUNCTION(BlueprintPure, Category = "Enemy|Health")
	bool IsDead() const;

	UFUNCTION(BlueprintPure, Category = "Enemy|Health")
	float GetHealthPercent() const;

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Health")
	FOnEnemyHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Health")
	FOnEnemyDied OnEnemyDied;

protected:
	virtual void BeginPlay() override;

	void Die();
	void SpawnResourceDrops();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Health",
		meta = (ClampMin = "1.0"))
	float MaxHealth = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Loot")
	TArray<FKapsResourceDrop> ResourceDrops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Loot",
		meta = (ClampMin = "0.0", Units = "cm"))
	float LootScatterRadius = 80.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Health")
	float CurrentHealth = 50.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Health")
	bool bIsDead = false;
};
