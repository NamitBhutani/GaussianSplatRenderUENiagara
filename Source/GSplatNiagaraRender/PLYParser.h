// PLYParser.h
// Parser for PLY files containing Gaussian Splat data

#pragma once

#include "CoreMinimal.h"
#include "GaussianSplatData.h"

/**
 * Enum representing the PLY file format type
 */
enum class EPLYFormat : uint8
{
	ASCII,
	BinaryLittleEndian,
	BinaryBigEndian,
	Unknown
};

/**
 * Represents a property defined in the PLY header
 */
struct FPLYProperty
{
	FString Name;
	FString Type;
	int32 ByteSize;
	bool bIsList;
	FString ListCountType;
	FString ListElementType;

	FPLYProperty()
		: ByteSize(0)
		, bIsList(false)
	{
	}

	static int32 GetTypeByteSize(const FString& TypeName)
	{
		if (TypeName == TEXT("float") || TypeName == TEXT("float32")) return 4;
		if (TypeName == TEXT("double") || TypeName == TEXT("float64")) return 8;
		if (TypeName == TEXT("int") || TypeName == TEXT("int32")) return 4;
		if (TypeName == TEXT("uint") || TypeName == TEXT("uint32")) return 4;
		if (TypeName == TEXT("short") || TypeName == TEXT("int16")) return 2;
		if (TypeName == TEXT("ushort") || TypeName == TEXT("uint16")) return 2;
		if (TypeName == TEXT("char") || TypeName == TEXT("int8")) return 1;
		if (TypeName == TEXT("uchar") || TypeName == TEXT("uint8")) return 1;
		return 4; // Default to float size
	}
};

class GSPLATNIAGARARENDER_API FPLYParser
{
public:
	FPLYParser();
	~FPLYParser();
	
	bool ParseFile(const FString& FilePath, TArray<FGaussianSplatData>& OutSplats);

	int32 GetVertexCount() const { return VertexCount; }

	EPLYFormat GetFormat() const { return Format; }

	FString GetErrorMessage() const { return ErrorMessage; }

private:
	bool ParseHeader(const TArray<FString>& Lines, int32& OutHeaderEndLine);

	bool ParseASCIIData(const TArray<FString>& Lines, int32 StartLine, TArray<FGaussianSplatData>& OutSplats);

	bool ParseBinaryData(const TArray<uint8>& FileData, int32 HeaderByteOffset, TArray<FGaussianSplatData>& OutSplats);

	float ReadFloat(const uint8* Data, int32& Offset, bool bBigEndian);

	double ReadDouble(const uint8* Data, int32& Offset, bool bBigEndian);

	int32 FindPropertyIndex(const FString& Name) const;

	int32 CalculateVertexByteSize() const;

	FGaussianSplatData ExtractSplatData(const TArray<float>& PropertyValues);

private:
	EPLYFormat Format;
	int32 VertexCount;
	TArray<FPLYProperty> Properties;
	FString ErrorMessage;

	// Property indices for quick lookup
	int32 PropIdx_X, PropIdx_Y, PropIdx_Z;
	int32 PropIdx_NX, PropIdx_NY, PropIdx_NZ;
	int32 PropIdx_FDC0, PropIdx_FDC1, PropIdx_FDC2;
	int32 PropIdx_Opacity;
	int32 PropIdx_Scale0, PropIdx_Scale1, PropIdx_Scale2;
	int32 PropIdx_Rot0, PropIdx_Rot1, PropIdx_Rot2, PropIdx_Rot3;
	TArray<int32> PropIdx_FRest;
};
