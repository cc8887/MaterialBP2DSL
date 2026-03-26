// MatBP2FPRoundTripCommandlet.h
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MatBP2FPRoundTripCommandlet.generated.h"

/**
 * Commandlet for round-trip validation of material DSL pipeline
 * Usage: -run=MatBP2FPRoundTrip
 */
UCLASS()
class UMatBP2FPRoundTripCommandlet : public UCommandlet
{
	GENERATED_BODY()
	
public:
	UMatBP2FPRoundTripCommandlet();
	virtual int32 Main(const FString& Params) override;
};
