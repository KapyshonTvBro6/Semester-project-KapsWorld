#include "KapsWorld/AI/KapsRangedAIController.h"

#include "KapsWorld/AI/KapsRangedEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void AKapsRangedAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    HomeLocation = InPawn->GetActorLocation();

    GetWorldTimerManager().SetTimer(
        AIUpdateTimer,
        this,
        &AKapsRangedAIController::UpdateAI,
        UpdateInterval,
        true
    );

    UpdateAI();
}

void AKapsRangedAIController::OnUnPossess()
{
    GetWorldTimerManager().ClearTimer(AIUpdateTimer);

    Super::OnUnPossess();
}

void AKapsRangedAIController::UpdateAI()
{
    AKapsRangedEnemy* Enemy = Cast<AKapsRangedEnemy>(GetPawn());

    if (!IsValid(Enemy) || Enemy->IsDead())
    {
        StopMovement();
        ClearFocus(EAIFocusPriority::Gameplay);
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
        DistanceToPlayer <= DetectionRange && LineOfSightTo(PlayerPawn);

    const float CurrentTime = GetWorld()->GetTimeSeconds();

    if (bCanSeePlayer)
    {
        LastKnownPlayerLocation = PlayerPawn->GetActorLocation();
        LastSeenPlayerTime = CurrentTime;

        SetFocus(PlayerPawn);

        if (DistanceToPlayer > PreferredMaxRange)
        {
            MoveToActor(PlayerPawn, PreferredMaxRange * 0.9f, false);
            return;
        }

        if (DistanceToPlayer < PreferredMinRange)
        {
            FVector RetreatDirection =
                Enemy->GetActorLocation() - PlayerPawn->GetActorLocation();

            RetreatDirection.Z = 0.0f;
            RetreatDirection.Normalize();

            const FVector RetreatLocation =
                Enemy->GetActorLocation() +
                RetreatDirection * RetreatDistance;

            MoveToLocation(RetreatLocation, 80.0f);
            return;
        }

        StopMovement();
        Enemy->TryRangedAttack(PlayerPawn);
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