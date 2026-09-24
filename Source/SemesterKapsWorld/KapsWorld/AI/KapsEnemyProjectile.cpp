#include "KapsWorld/AI/KapsEnemyProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AKapsEnemyProjectile::AKapsEnemyProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    InitialLifeSpan = 5.0f;

    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    Collision->InitSphereRadius(18.0f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetNotifyRigidBodyCollision(true);
    RootComponent = Collision;

    VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
    VisualMesh->SetupAttachment(Collision);
    VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisualMesh->SetRelativeScale3D(FVector(0.35f));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
        TEXT("/Engine/BasicShapes/Sphere.Sphere")
    );

    if (SphereMesh.Succeeded())
    {
        VisualMesh->SetStaticMesh(SphereMesh.Object);
    }

    ProjectileMovement =
        CreateDefaultSubobject<UProjectileMovementComponent>(
            TEXT("ProjectileMovement")
        );

    ProjectileMovement->UpdatedComponent = Collision;
    ProjectileMovement->InitialSpeed = Speed;
    ProjectileMovement->MaxSpeed = Speed;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->ProjectileGravityScale = 0.0f;

    Collision->OnComponentHit.AddDynamic(
        this,
        &AKapsEnemyProjectile::OnProjectileHit
    );
}

void AKapsEnemyProjectile::InitializeProjectile(
    float InDamage,
    float InSpeed,
    AController* InInstigatorController
)
{
    Damage = InDamage;
    Speed = InSpeed;
    InstigatorController = InInstigatorController;

    ProjectileMovement->InitialSpeed = Speed;
    ProjectileMovement->MaxSpeed = Speed;
    ProjectileMovement->Velocity = GetActorForwardVector() * Speed;

    if (GetOwner())
    {
        Collision->IgnoreActorWhenMoving(GetOwner(), true);
    }
}

void AKapsEnemyProjectile::OnProjectileHit(
    UPrimitiveComponent* HitComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    FVector NormalImpulse,
    const FHitResult& Hit
)
{
    if (IsValid(OtherActor) && OtherActor != GetOwner())
    {
        UGameplayStatics::ApplyDamage(
            OtherActor,
            Damage,
            InstigatorController,
            GetOwner(),
            nullptr
        );
    }

    Destroy();
}
