#include "KapsWorld/AI/KapsDashEnemy.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AKapsDashEnemy::AKapsDashEnemy()
{
    GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;
}

void AKapsDashEnemy::BeginPlay()
{
    Super::BeginPlay();

    GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;
}

bool AKapsDashEnemy::CanStartDash() const
{
    return !IsDead() && !bIsDashing && !bDashOnCooldown;
}

bool AKapsDashEnemy::StartDash(AActor* Target)
{
    if (!CanStartDash() || !IsValid(Target))
    {
        return false;
    }

    DashTarget = Target;
    bIsDashing = true;
    bDashOnCooldown = true;

    DashDirection =
        Target->GetActorLocation() - GetActorLocation();

    DashDirection.Z = 0.0f;
    DashDirection.Normalize();

    SetActorRotation(DashDirection.Rotation());

    GetWorldTimerManager().SetTimer(
        WindupTimer,
        this,
        &AKapsDashEnemy::PerformDash,
        WindupDuration,
        false
    );

    return true;
}

void AKapsDashEnemy::PerformDash()
{
    LaunchCharacter(DashDirection * DashSpeed, true, false);

    GetWorldTimerManager().SetTimer(
        DashTimer,
        this,
        &AKapsDashEnemy::EndDash,
        DashDuration,
        false
    );
}

void AKapsDashEnemy::EndDash()
{
    GetCharacterMovement()->StopMovementImmediately();

    if (IsValid(DashTarget))
    {
        const float DistanceToTarget = FVector::Dist2D(
            GetActorLocation(),
            DashTarget->GetActorLocation()
        );

        if (DistanceToTarget <= DashHitRange)
        {
            UGameplayStatics::ApplyDamage(
                DashTarget,
                DashDamage,
                GetController(),
                this,
                nullptr
            );
        }
    }

    bIsDashing = false;

    GetWorldTimerManager().SetTimer(
        CooldownTimer,
        this,
        &AKapsDashEnemy::ResetDashCooldown,
        DashCooldown,
        false
    );
}

void AKapsDashEnemy::ResetDashCooldown()
{
    bDashOnCooldown = false;
}