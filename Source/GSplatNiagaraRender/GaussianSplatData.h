
#pragma once

#include "CoreMinimal.h"
#include "GaussianSplatData.generated.h"


USTRUCT(BlueprintType)
struct GSPLATNIAGARARENDER_API FGaussianSplatData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	FVector Position;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	FVector Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	FQuat Orientation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	FVector Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	float Opacity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	FVector ZeroOrderHarmonicsCoefficients;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gaussian Splat")
	TArray<FVector> HighOrderHarmonicsCoefficients;

	FGaussianSplatData()
		: Position(FVector::ZeroVector)
		, Normal(FVector::ZeroVector)
		, Orientation(FQuat::Identity)
		, Scale(FVector::OneVector)
		, Opacity(0.0f)
		, ZeroOrderHarmonicsCoefficients(FVector::ZeroVector)
	{
	}

	static FVector ConvertPositionToUnreal(float X, float Y, float Z)
	{
		return FVector(X, -Z, -Y) * 100.0f;
	}

	static FVector ConvertScaleToUnreal(float ScaleX, float ScaleY, float ScaleZ)
	{
		auto Sigmoid = [](float Val) { return 1.0f / (1.0f + FMath::Exp(-Val)); };
		return FVector(Sigmoid(ScaleX), Sigmoid(ScaleY), Sigmoid(ScaleZ)) * 100.0f;
	}

	static FQuat ConvertOrientationToUnreal(float W, float X, float Y, float Z)
	{
		FQuat Quat(X, Y, Z, W);
		Quat.Normalize();
		return Quat;
	}

	static float ConvertOpacityToUnreal(float RawOpacity)
	{
		return 1.0f / (1.0f + FMath::Exp(-RawOpacity));
	}

	static FLinearColor SHToColor(const FVector& SHCoeffs)
	{
		// SH0 constant = 0.28209479177387814
		const float C0 = 0.28209479177387814f;
		float R = SHCoeffs.X * C0 + 0.5f;
		float G = SHCoeffs.Y * C0 + 0.5f;
		float B = SHCoeffs.Z * C0 + 0.5f;
		return FLinearColor(FMath::Clamp(R, 0.0f, 1.0f), FMath::Clamp(G, 0.0f, 1.0f), FMath::Clamp(B, 0.0f, 1.0f), 1.0f);
	}
};
