#pragma once

#include "CoreMinimal.h"
#include "GaussianSplatData.generated.h"

/**
 * Represents parsed data for a single splat, loaded from a PLY file.
 */
USTRUCT(BlueprintType)
struct FGaussianSplatData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FVector3f Position;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FVector3f Normal;

    // Splat orientation coming as wxyz from PLY (rot_0, rot_1, rot_2, rot_3)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FQuat4f Orientation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FVector3f Scale;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    float Opacity;

    // Spherical Harmonics coefficients - Zero order (f_dc_0, f_dc_1, f_dc_2)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    FVector3f ZeroOrderHarmonicsCoefficients;

    // Spherical Harmonics coefficients - High order
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
    TArray<FVector3f> HighOrderHarmonicsCoefficients;

    FGaussianSplatData()
        : Position(FVector3f::ZeroVector), Normal(FVector3f::ZeroVector), Orientation(FQuat4f::Identity),
          Scale(FVector3f::OneVector), Opacity(0.0f), ZeroOrderHarmonicsCoefficients(FVector3f::ZeroVector)
    {
    }

    // Convert Y-up Right Handed (PLY) to Z-up Left Handed (UE)
    static FVector3f ConvertPositionToUnreal(float X, float Y, float Z)
    {
        return FVector3f(X, -Z, -Y) * 100.0f;
    }

    // Sigmoid Activation for Scale
    static FVector3f ConvertScaleToUnreal(float X, float Y, float Z)
    {
        auto Sigmoid = [](float v) { return 1.0f / (1.0f + FMath::Exp(-v)); };
        return FVector3f(Sigmoid(X), Sigmoid(Y), Sigmoid(Z)) * 100.0f;
    }

    // Normalize Quaternion
    static FQuat4f ConvertOrientationToUnreal(float W, float X, float Y, float Z)
    {
        FQuat4f Q(X, Y, Z, W);
        Q.Normalize();
        return Q;
    }

    // Sigmoid for Opacity
    static float ConvertOpacityToUnreal(float O)
    {
        return 1.0f / (1.0f + FMath::Exp(-O));
    }

    // SH0 to linear color
    static FLinearColor SHToColor(const FVector3f &SHCoeffs)
    {
        const float C0 = 0.28209479177387814f;
        float R = SHCoeffs.X * C0 + 0.5f;
        float G = SHCoeffs.Y * C0 + 0.5f;
        float B = SHCoeffs.Z * C0 + 0.5f;
        return FLinearColor(FMath::Clamp(R, 0.0f, 1.0f), FMath::Clamp(G, 0.0f, 1.0f), FMath::Clamp(B, 0.0f, 1.0f),
                            1.0f);
    }
};
