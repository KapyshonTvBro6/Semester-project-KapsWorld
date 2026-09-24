#include "KapsWorld/AI/KapsDashAIController.h"

#include "KapsWorld/AI/KapsDashEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void AKapsDashAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    HomeLocation = InPawn->GetActorLocation();

    GetWorldTimerManager().SetTimer(
        AIUpdateTimer,
        this,
        &AKapsDashAIController::UpdateAI,
        UpdateInterval,
        true
    );

    UpdateAI();
}

void AKapsDashAIController::OnUnPossess()
{
    GetWorldTimerManager().ClearTimer(AIUpdateTimer);

    Super::OnUnPossess();
}

void AKapsDashAIController::UpdateAI()
{
    AKapsDashEnemy* Enemy = Cast<AKapsDashEnemy>(GetPawn());

    if (!IsValid(Enemy) || Enemy->IsDead())
    {
        StopMovement();
        ClearFocus(EAIFocusPriority::Gameplay);
        return;
    }

    if (Enemy->IsDashing())
    {
        return;
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);

    if (!IsValid(PlayerPawn))
    {
        return;
    }

    const float DistanceToPlayer = FVector::Dist2D(
        Enemy->GetActorLocation(),
        PlayerPawn->GetActorLocation()
    );

    const bool bCanSeePlayer =
        DistanceToPlayer <= DetectionRange &&
        LineOfSightTo(PlayerPawn);

    const float CurrentTime = GetWorld()->GetTimeSeconds();

    if (bCanSeePlayer)
    {
        LastKnownPlayerLocation = PlayerPawn->GetActorLocation();
        LastSeenPlayerTime = CurrentTime;

        SetFocus(PlayerPawn);

        if (Enemy->CanStartDash() &&
            DistanceToPlayer >= Enemy->GetDashMinRange() &&
            DistanceToPlayer <= Enemy->GetDashMaxRange())
        {
            StopMovement();
            Enemy->StartDash(PlayerPawn);
            return;
        }

        if (DistanceToPlayer > Enemy->GetDashMaxRange())
        {
            MoveToActor(
                PlayerPawn,
                Enemy->GetDashMaxRange() * 0.85f,
                false
            );

            return;
        }

        if (DistanceToPlayer < Enemy->GetDashMinRange())
        {
            FVector AwayDirection =
                Enemy->GetActorLocation() -
                PlayerPawn->GetActorLocation();

            AwayDirection.Z = 0.0f;
            AwayDirection.Normalize();

            MoveToLocation(
                Enemy->GetActorLocation() + AwayDirection * 300.0f,
                80.0f
            );

            return;
        }

        StopMovement();
        return;
    }

    ClearFocus(EAIFocusPriority::Gameplay);

    if (CurrentTime - LastSeenPlayerTime <= LostTargetDelay)
    {
        MoveToLocation(LastKnownPlayerLocation, 80.0f);
        return;
    }

    MoveToLocation(HomeLocation, HomeAcceptanceRadius);
}