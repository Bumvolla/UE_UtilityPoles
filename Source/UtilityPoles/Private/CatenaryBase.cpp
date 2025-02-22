// Fill out your copyright notice in the Description page of Project Settings.

#include "CatenaryBase.h"
#include "UtilityPoles.h"

// Sets default values
ACatenaryBase::ACatenaryBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACatenaryBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

    if (autoGenerate) Generate();
}

void ACatenaryBase::Generate()
{

}

void ACatenaryBase::SetMeshLenght()
{
    MeshLenght = USplineHelpers::GetMeshLenght(WireMesh, WireMeshAxis);
}

void ACatenaryBase::RemoveExcesSplines(int32 NeededSplines)
{
    int16 wiresAmmount = AllWires.Num();

    if (NeededSplines < wiresAmmount)
    {
        for (int16 i = NeededSplines; i < wiresAmmount; i++)
        {
            AllWires[i]->DestroyComponent();
        }

    }
    AllWires.SetNum(NeededSplines);
}

void ACatenaryBase::RemoveSplines()
{
    for (USplineComponent* SplineComp : AllWires)
    {
        SplineComp->DestroyComponent();
    }
    AllWires.Empty();
}

void ACatenaryBase::RemoveSplineMeshes()
{
    if (!AllSplineMeshes.IsEmpty())
    {
        for (USplineMeshComponent* splineMesh : AllSplineMeshes)
        {
            splineMesh->DestroyComponent();
        }
        AllSplineMeshes.Empty();
    }
}

void ACatenaryBase::ReuseOrCreateSplines()
{

    for (int16 i = 0; i < AllWires.Num(); i++)
    {
        USplineComponent* Wire;

        if (!AllWires[i])
        {
            Wire = NewObject<USplineComponent>(this);
            Wire->RegisterComponent();            
            Wire->SetMobility(EComponentMobility::Static);
            AllWires[i]=Wire;
        }

    }

}

TArray<FVector> ACatenaryBase::CalculateSingleCatenary(TArray<FVector> ConectionPoints, bool bIsClosedLoop)
{
    TArray<FVector> AllCatenaryPoints;

    if (ConectionPoints.Num() == 0)
    {
        return AllCatenaryPoints;
    }

    FTransform actorPos = GetActorTransform();

    bool bIsLast = false;

    for (int32 i = 0; i < ConectionPoints.Num(); i++)
    {
        const FVector currentPos = actorPos.InverseTransformPosition(ConectionPoints[i]);
        FVector nextPos;

        if(ConectionPoints.IsValidIndex(i+1))
        {
            nextPos = actorPos.InverseTransformPosition(ConectionPoints[i+1]);
            if (!ConectionPoints.IsValidIndex(i + 2))
            {
                bIsLast = true;
            }
        }
        else
        {
            bIsLast = true;
            if (bIsClosedLoop)
            {
                nextPos = actorPos.InverseTransformPosition(ConectionPoints[0]);
            }
            else break;
        }

        TArray<FVector> CatenaryPoints = UCatenaryHelpers::CreateCatenaryNewton(
            currentPos,
            nextPos,
            Slack,
            SlackVariation,
            SplineResolution);

        if (!bIsLast)
        {
            CatenaryPoints.Pop();
            AllCatenaryPoints.Append(CatenaryPoints);
        }
        else
        {
            AllCatenaryPoints.Append(CatenaryPoints);
            break;
        }

    }

    return AllCatenaryPoints;

}

TArray<TArray<FVector>> ACatenaryBase::CalculateCatenariesParalel(const TArray<AUtilityPolePreset*>& ConectionPoints, bool bIsClosedLoop)
{

    TArray<TArray<FVector>> AllCatenaryPoints;
    TArray<FVector> localSpaceWireTargets;

    if (ConectionPoints.IsEmpty()) return AllCatenaryPoints;

    if (!ConectionPoints[0]) return AllCatenaryPoints;

    localSpaceWireTargets = ConectionPoints[0]->WireTargets;

    // Pre-calculate all catenary points in parallel
    const int32 WiresNum = localSpaceWireTargets.Num();
    AllCatenaryPoints.SetNum(WiresNum);


    ParallelFor(WiresNum, [&](int32 i) {
        for (int32 j = 0; j < ConectionPoints.Num(); j++)
        {
            bool bIsLast = false;

            FTransform currentPos = ConectionPoints[j]->GetActorTransform();
            FTransform nextPos;

            if (ConectionPoints.IsValidIndex(j + 1))
            {
                nextPos = ConectionPoints[j + 1]->GetActorTransform();

                if (!ConectionPoints.IsValidIndex(j + 2))
                {
                    bIsLast = true;
                }
            }
            else
            {
                bIsLast = true;
                if (bIsClosedLoop)
                {
                    nextPos = ConectionPoints[0]->GetActorTransform();
                }
                else break;
            }

            TArray<FVector> CatenaryPoints = UCatenaryHelpers::CreateCatenaryNewton(
                currentPos.TransformPosition(localSpaceWireTargets[i]),
                nextPos.TransformPosition(localSpaceWireTargets[i]),
                Slack,
                SlackVariation,
                SplineResolution);

            if (CatenaryPoints.IsEmpty()) return;

            if (!bIsLast)
            {
                CatenaryPoints.Pop();
                AllCatenaryPoints[i].Append(CatenaryPoints);
            }
            else
            {
                AllCatenaryPoints[i].Append(CatenaryPoints);
                break;
            }
        }

    });
    return AllCatenaryPoints;
}

void ACatenaryBase::ConstructSplineMeshesAlongSplines(USplineComponent* Spline)
{

    const float MeshLength = USplineHelpers::GetMeshLenght(WireMesh, WireMeshAxis);
    int32 SegmentStartPoint = 0;
    int32 SegmentEndPoint = SplineResolution - 1;

    while (SegmentEndPoint < Spline->GetNumberOfSplinePoints())
    {
        int32 MeshCount = USplineHelpers::GetMeshCountBewteenSplinePoints(Spline, WireMesh, WireMeshAxis, SegmentStartPoint, SegmentEndPoint);

        float SegmentStartDistance = Spline->GetDistanceAlongSplineAtSplinePoint(SegmentStartPoint);

        for (int32 i = 0; i < MeshCount; i++)
        {

            USplineMeshComponent* SplineMesh;

            if (AvailableSplineMeshes.Num() > 0)
            {
                SplineMesh = AvailableSplineMeshes.Pop();
                if (SplineMesh->GetStaticMesh() != WireMesh) SplineMesh->SetStaticMesh(WireMesh);
            }
            else
            {
                SplineMesh = NewObject<USplineMeshComponent>(this);
                SplineMesh->RegisterComponent();
                SplineMesh->AttachToComponent(Spline, FAttachmentTransformRules::KeepRelativeTransform);
                SplineMesh->SetForwardAxis(FindAxis(WireMeshAxis));
                SplineMesh->SetStaticMesh(WireMesh);
            }

            FVector StartPoint, StartTangent, EndPoint, EndTangent;

            if (i == 0)
            {

                Spline->GetLocationAndTangentAtSplinePoint(SegmentStartPoint, StartPoint, StartTangent, ESplineCoordinateSpace::Local);


                float EndDistance = SegmentStartDistance + MeshLength;
                EndPoint = Spline->GetLocationAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::Local);
                EndTangent = Spline->GetTangentAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::Local).GetClampedToSize(0, MeshLength);
            }

            else if (i == MeshCount - 1)
            {

                float StartDistance = SegmentStartDistance + (i * MeshLength);
                StartPoint = Spline->GetLocationAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::Local);
                StartTangent = Spline->GetTangentAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::Local).GetClampedToSize(0, MeshLength);

                Spline->GetLocationAndTangentAtSplinePoint(SegmentEndPoint, EndPoint, EndTangent, ESplineCoordinateSpace::Local);
            }

            else
            {
                USplineHelpers::GetSplineMeshStartAndEndByIteration(
                    i,
                    MeshLength,
                    Spline,
                    StartPoint,
                    StartTangent,
                    EndPoint,
                    EndTangent,
                    SegmentStartDistance);
            }

            SplineMesh->SetStartAndEnd(StartPoint, StartTangent, EndPoint, EndTangent);


            AllSplineMeshes.Add(SplineMesh);
        }

        // Move to next segment
        SegmentStartPoint = SegmentEndPoint;
        SegmentEndPoint += SplineResolution - 1;
    }

}

void ACatenaryBase::SnapToTerrain(const FTransform& OgTransform, FTransform& SnapedTransform, ECoordSystem CoordSystem)
{
    FVector WorldLocation = OgTransform.GetLocation();
    if (CoordSystem != ECoordSystem::COORD_World)
    {
        // Convert to worldspace for raycasting
        WorldLocation = GetActorTransform().TransformPosition(OgTransform.GetLocation());
    }
    const FVector RayStartLocation = FVector(WorldLocation.X, WorldLocation.Y, WorldLocation.Z + RayLength / 2);
    const FVector RayEndLocation = FVector(WorldLocation.X, WorldLocation.Y, WorldLocation.Z - RayLength / 2);

    FHitResult HitResult;

    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(this);

    bool bHit = GetWorld()->LineTraceSingleByChannel(
        HitResult,
        RayStartLocation,
        RayEndLocation,
        CollisionChannel,
        CollisionParams
    );

    if (bDrawDebugLines)
    {
        DrawDebugLines(RayStartLocation, RayEndLocation, bHit, HitResult);
    }

    if (bHit)
    {

        FQuat ImpactNormalRotator;
        AlignToNormal(HitResult.ImpactNormal, ImpactNormalRotator);

        FVector ReturnPosition = HitResult.ImpactPoint;

        if (CoordSystem != ECoordSystem::COORD_World)
        {
            // Convert to worldspace for raycasting
            ReturnPosition = GetActorTransform().InverseTransformPosition(HitResult.ImpactPoint);
        }

        SnapedTransform = FTransform(bAlignToNormal ? ImpactNormalRotator : OgTransform.GetRotation(), ReturnPosition, OgTransform.GetScale3D());
    }
    else
    {
        SnapedTransform = OgTransform;
    }
}

void ACatenaryBase::AlignToNormal(const FVector& ImpactNormal, FQuat& NormalAlignedQuat)
{
    NormalAlignedQuat = FQuat::FindBetweenNormals(FVector::UpVector, GetActorTransform().InverseTransformVectorNoScale(ImpactNormal));
}

void ACatenaryBase::RandomizeTilt(const FTransform& OgTransform, FTransform& RandomRotatedTransform)
{
    FRotator storedRotator = OgTransform.Rotator();
    FRotator randomizedRotator = FRotator(FMath::RandRange(RandomTilt * -1, RandomTilt), storedRotator.Yaw, storedRotator.Roll);
    RandomRotatedTransform.SetRotation(randomizedRotator.Quaternion());
}

void ACatenaryBase::DrawDebugLines(FVector StartPoint, FVector EndPoint, bool bHit, FHitResult Hit)
{
    DrawDebugLine(
        GetWorld(),
        StartPoint,
        EndPoint,
        FColor::Red,
        false,
        10.f,
        0,
        5.f
    );
    if (bHit)
    {

        DrawDebugLine(
            GetWorld(),
            Hit.Location,
            Hit.Location,
            FColor::Green, false,
            10.f,
            0,
            20.f);

        DrawDebugLine(
            GetWorld(),
            Hit.Location,
            Hit.Location + Hit.ImpactNormal * 50.f,
            FColor::Blue, false,
            10.f,
            0,
            5.f);
    }
}

void ACatenaryBase::CleanupSplines()
{
    for (USplineComponent* Spline : AllWires)
    {
        Spline->DestroyComponent();
    }
    AllWires.Reset();
}


