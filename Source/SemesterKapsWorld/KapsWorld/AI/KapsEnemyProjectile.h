#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KapsEnemyProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

UCLASS()
class SEMESTERKAPSWORLD_API AKapsEnemyProjectile : public AActor
{
	GENERATED_BODY()

public:
	AKapsEnemyProjectile();

	void InitializeProjectile(
		float InDamage,
		float InSpeed,
		AController* InInstigatorController
	);

protected:
	UFUNCTION()
	void OnProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit
	);

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditAnywhere, Category = "Projectile",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float Speed = 1200.0f;

private:
	float Damage = 8.0f;

	UPROPERTY()
	TObjectPtr<AController> InstigatorController;
};
