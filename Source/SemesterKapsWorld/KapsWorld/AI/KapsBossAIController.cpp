#include "KapsWorld/AI/KapsBossAIController.h"

#include "KapsWorld/AI/KapsBossEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void AKapsBossAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	HomeLocation = InPawn->GetActorLocation();

	GetWorldTimerManager().SetTimer(
		AIUpdateTimer,
		this,
		&AKapsBossAIController::UpdateAI,
		UpdateInterval,
		true
	);
}

void AKapsBossAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(AIUpdateTimer);
	Super::OnUnPossess();
}

void AKapsBossAIController::UpdateAI()
{
	AKapsBossEnemy* Boss = Cast<AKapsBossEnemy>(GetPawn());

	if (!IsValid(Boss) || Boss->IsDead())
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
		Boss->GetActorLocation(),
		PlayerPawn->GetActorLocation()
	);

	const bool bCanSeePlayer = DistanceToPlayer <= DetectionRange &&
		LineOfSightTo(PlayerPawn);

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	if (bCanSeePlayer)
	{
		LastKnownPlayerLocation = PlayerPawn->GetActorLocation();
		LastSeenPlayerTime = CurrentTime;
		SetFocus(PlayerPawn);

		if (DistanceToPlayer > Boss->GetPreferredMaxRange())
		{
			MoveToActor(PlayerPawn, Boss->GetPreferredMaxRange() * 0.85f, false);
			return;
		}

		if (DistanceToPlayer < Boss->GetPreferredMinRange())
		{
			const FVector AwayDirection = (
				Boss->GetActorLocation() - PlayerPawn->GetActorLocation()
			).GetSafeNormal2D();

			MoveToLocation(Boss->GetActorLocation() + AwayDirection * 350.0f, 80.0f);
			return;
		}

		StopMovement();
		Boss->TryRangedAttack(PlayerPawn);
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
