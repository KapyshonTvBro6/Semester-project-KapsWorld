#pragma once

#include "CoreMinimal.h"
#include "KapsWorld/AI/KapsEnemyBase.h"
#include "KapsDashEnemy.generated.h"

UCLASS()
class SEMESTERKAPSWORLD_API AKapsDashEnemy : public AKapsEnemyBase
{
    GENERATED_BODY()

public:
    AKapsDashEnemy();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category = "Enemy|Dash")
    bool StartDash(AActor* Target);

    UFUNCTION(BlueprintPure, Category = "Enemy|Dash")
    bool CanStartDash() const;

    UFUNCTION(BlueprintPure, Category = "Enemy|Dash")
    bool IsDashing() const
    {
        return bIsDashing;
    }

    UFUNCTION(BlueprintPure, Category = "Enemy|Dash")
    float GetDashMinRange() const
    {
        return DashMinRange;
    }

    UFUNCTION(BlueprintPure, Category = "Enemy|Dash")
    float GetDashMaxRange() const
    {
        return DashMaxRange;
    }

protected:
    void PerformDash();
    void EndDash();
    void ResetDashCooldown();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Movement",
        meta = (ClampMin = "1.0", Units = "cm/s"))
    float MovementSpeed = 280.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "1.0", Units = "cm"))
    float DashMinRange = 250.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "1.0", Units = "cm"))
    float DashMaxRange = 700.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "0.0", Units = "s"))
    float WindupDuration = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "1.0", Units = "cm/s"))
    float DashSpeed = 1600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "0.05", Units = "s"))
    float DashDuration = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash")
    float DashDamage = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "1.0", Units = "cm"))
    float DashHitRange = 140.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Dash",
        meta = (ClampMin = "0.1", Units = "s"))
    float DashCooldown = 3.0f;

private:
    bool bIsDashing = false;
    bool bDashOnCooldown = false;

    FVector DashDirection;
    TObjectPtr<AActor> DashTarget;

    FTimerHandle WindupTimer;
    FTimerHandle DashTimer;
    FTimerHandle CooldownTimer;
};