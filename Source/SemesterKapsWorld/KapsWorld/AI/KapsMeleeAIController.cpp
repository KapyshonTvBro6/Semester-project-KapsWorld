// KapsMeleeAIController.cpp
#include "KapsWorld/AI/KapsMeleeAIController.h"

#include "KapsWorld/AI/KapsMeleeEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void AKapsMeleeAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	HomeLocation = InPawn->GetActorLocation();

	GetWorldTimerManager().SetTimer(
		AIUpdateTimer,
		this,
		&AKapsMeleeAIController::UpdateAI,
		UpdateInterval,
		true
	);

	UpdateAI();
}

void AKapsMeleeAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(AIUpdateTimer);

	Super::OnUnPossess();
}

void AKapsMeleeAIController::UpdateAI()
{
	AKapsMeleeEnemy* Enemy = Cast<AKapsMeleeEnemy>(GetPawn());

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

	const float DistanceToPlayer =
		FVector::Dist(Enemy->GetActorLocation(), PlayerPawn->GetActorLocation());

	const bool bPlayerInDetectionRange = DistanceToPlayer <= DetectionRange;
	const bool bCanSeePlayer =
		bPlayerInDetectionRange && LineOfSightTo(PlayerPawn);

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	if (bCanSeePlayer)
	{
		LastKnownPlayerLocation = PlayerPawn->GetActorLocation();
		LastSeenPlayerTime = CurrentTime;

		SetFocus(PlayerPawn);

		if (DistanceToPlayer <= Enemy->GetAttackRange())
		{
			StopMovement();
			Enemy->TryAttack(PlayerPawn);
		}
		else if (!bIsChasing)
		{
			MoveToActor(PlayerPawn, Enemy->GetAttackRange() * 0.8f);
			bIsChasing = true;
		}

		return;
	}

	ClearFocus(EAIFocusPriority::Gameplay);
	bIsChasing = false;

	const bool bRecentlySawPlayer =
		CurrentTime - LastSeenPlayerTime <= LostTargetDelay;

	if (bRecentlySawPlayer)
	{
		MoveToLocation(LastKnownPlayerLocation, 80.0f);
		return;
	}

	MoveToLocation(HomeLocation, HomeAcceptanceRadius);
}