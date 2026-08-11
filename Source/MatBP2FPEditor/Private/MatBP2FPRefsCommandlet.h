// Query forward and reverse material-function references from an export index.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MatBP2FPRefsCommandlet.generated.h"

UCLASS()
class UMatBP2FPRefsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMatBP2FPRefsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
