// MatBP2FPModule.h - Runtime Module Interface
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UObject;
class UEdGraph;
class UEdGraphNode;

namespace MatBP2FPImportLifecycle
{
	enum class EImportLifecyclePhase : uint8
	{
		PreNodeChanges,
		PostNodeChanges,
		PrePropertyChanges,
		PostPropertyChanges,
		PreFinalize,
		PostFinalize,
	};

	enum class EImportNodeChangeType : uint8
	{
		Added,
		Modified,
		Removed,
	};

	enum class EImportPropertyChangeType : uint8
	{
		Added,
		Modified,
		Removed,
	};

	struct MATBP2FP_API FImportLifecycleContext
	{
		FGuid ImportSessionId;
		UObject* TargetAsset = nullptr;
		UEdGraph* TargetGraph = nullptr;
		FName ScopeName;
		bool bIsFullRebuild = false;
		bool bIsIncremental = false;
		bool bIsHeadless = false;
		bool bWillCompile = false;
		TSet<FName> RequestedBehaviors;
	};

	struct MATBP2FP_API FImportNodeChange
	{
		UEdGraphNode* Node = nullptr;
		EImportNodeChangeType ChangeType = EImportNodeChangeType::Added;
	};

	struct MATBP2FP_API FImportPropertyChange
	{
		UObject* TargetObject = nullptr;
		FName PropertyName;
		EImportPropertyChangeType ChangeType = EImportPropertyChangeType::Modified;
	};

	struct MATBP2FP_API FImportNodePhaseEvent
	{
		EImportLifecyclePhase Phase = EImportLifecyclePhase::PreNodeChanges;
		FImportLifecycleContext Context;
		TArray<FImportNodeChange> Changes;
	};

	struct MATBP2FP_API FImportPropertyPhaseEvent
	{
		EImportLifecyclePhase Phase = EImportLifecyclePhase::PrePropertyChanges;
		FImportLifecycleContext Context;
		TArray<FImportPropertyChange> Changes;
	};

	struct MATBP2FP_API FImportFinalizePhaseEvent
	{
		EImportLifecyclePhase Phase = EImportLifecyclePhase::PreFinalize;
		FImportLifecycleContext Context;
	};

	struct MATBP2FP_API FImportLifecycleHookHandle
	{
		FGuid Id;

		bool IsValid() const
		{
			return Id.IsValid();
		}

		friend bool operator==(const FImportLifecycleHookHandle& Lhs, const FImportLifecycleHookHandle& Rhs)
		{
			return Lhs.Id == Rhs.Id;
		}
	};

	class MATBP2FP_API IImportLifecycleHook
	{
	public:
		virtual ~IImportLifecycleHook() = default;

		virtual int32 GetPriority(EImportLifecyclePhase Phase) const
		{
			return 0;
		}

		virtual void OnNodePhase(const FImportNodePhaseEvent& Event) {}
		virtual void OnPropertyPhase(const FImportPropertyPhaseEvent& Event) {}
		virtual void OnFinalizePhase(const FImportFinalizePhaseEvent& Event) {}
	};
}

class MATBP2FP_API IMatBP2FPImportHookHost
{
public:
	virtual ~IMatBP2FPImportHookHost() = default;

	virtual MatBP2FPImportLifecycle::FImportLifecycleHookHandle RegisterImportLifecycleHook(
		TSharedRef<MatBP2FPImportLifecycle::IImportLifecycleHook> Hook) = 0;

	virtual void UnregisterImportLifecycleHook(
		MatBP2FPImportLifecycle::FImportLifecycleHookHandle Handle) = 0;
};

class MATBP2FP_API FMatBP2FPModule : public IModuleInterface, public IMatBP2FPImportHookHost
{
public:
	static inline FMatBP2FPModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMatBP2FPModule>("MatBP2FP");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MatBP2FP");
	}

	virtual MatBP2FPImportLifecycle::FImportLifecycleHookHandle RegisterImportLifecycleHook(
		TSharedRef<MatBP2FPImportLifecycle::IImportLifecycleHook> Hook) override;

	virtual void UnregisterImportLifecycleHook(
		MatBP2FPImportLifecycle::FImportLifecycleHookHandle Handle) override;

	void BroadcastNodePhase(const MatBP2FPImportLifecycle::FImportNodePhaseEvent& Event);
	void BroadcastPropertyPhase(const MatBP2FPImportLifecycle::FImportPropertyPhaseEvent& Event);
	void BroadcastFinalizePhase(const MatBP2FPImportLifecycle::FImportFinalizePhaseEvent& Event);

	struct FRegisteredImportHook
	{
		MatBP2FPImportLifecycle::FImportLifecycleHookHandle Handle;
		TSharedRef<MatBP2FPImportLifecycle::IImportLifecycleHook> Hook;
		int64 RegistrationOrder = 0;
	};

private:
	TArray<FRegisteredImportHook> RegisteredHooks;
	int64 NextRegistrationOrder = 0;
};
