#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KapsWeaponBase.generated.h"

UCLASS(Abstract, Blueprintable)
class SEMESTERKAPSWORLD_API AKapsWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AKapsWeaponBase();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Attack")
	bool TryPrimaryAttack();

	UFUNCTION(BlueprintPure, Category = "Weapon|Attack")
	bool IsPrimaryAttackReady() const;

protected:
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Weapon|Attack")
	bool CanPerformPrimaryAttack() const;

	virtual bool CanPerformPrimaryAttack_Implementation() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Attack")
	void PerformPrimaryAttack();

	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "Weapon|Stats",
		meta = (ClampMin = "0.0")
	)
	float Damage = 25.0f;

	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "Weapon|Attack",
		meta = (ClampMin = "0.0", Units = "s")
	)
	float AttackCooldown = 0.5f;

private:
	double LastAttackTime = -1.0e9;
};