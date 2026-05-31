// MatBP2FPModule.cpp - Runtime Module Entry
// Copyright (c) 2026 OpenClaw Research. All Rights Reserved.

#include "MatBP2FPModule.h"
#include "Algo/Sort.h"

namespace
{
	using namespace MatBP2FPImportLifecycle;

	template <typename EventType, typename CallbackType>
	void MBP_SortAndBroadcastHooks(
		const TArray<FMatBP2FPModule::FRegisteredImportHook>& RegisteredHooks,
		EImportLifecyclePhase Phase,
		const EventType& Event,
		CallbackType&& Callback)
	{
		TArray<FMatBP2FPModule::FRegisteredImportHook> Hooks = RegisteredHooks;
		Algo::Sort(Hooks, [Phase](const FMatBP2FPModule::FRegisteredImportHook& Lhs,
			const FMatBP2FPModule::FRegisteredImportHook& Rhs)
		{
			const int32 LhsPriority = Lhs.Hook->GetPriority(Phase);
			const int32 RhsPriority = Rhs.Hook->GetPriority(Phase);
			if (LhsPriority != RhsPriority)
			{
				return LhsPriority > RhsPriority;
			}
			return Lhs.RegistrationOrder < Rhs.RegistrationOrder;
		});

		for (const FMatBP2FPModule::FRegisteredImportHook& Entry : Hooks)
		{
			Callback(*Entry.Hook, Event);
		}
	}
}

MatBP2FPImportLifecycle::FImportLifecycleHookHandle FMatBP2FPModule::RegisterImportLifecycleHook(
	TSharedRef<MatBP2FPImportLifecycle::IImportLifecycleHook> Hook)
{
	MatBP2FPImportLifecycle::FImportLifecycleHookHandle Handle;
	Handle.Id = FGuid::NewGuid();

	FRegisteredImportHook Entry{Handle, Hook, NextRegistrationOrder++};
	RegisteredHooks.Add(MoveTemp(Entry));
	return Handle;
}

void FMatBP2FPModule::UnregisterImportLifecycleHook(
	MatBP2FPImportLifecycle::FImportLifecycleHookHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	RegisteredHooks.RemoveAll([&Handle](const FRegisteredImportHook& Entry)
	{
		return Entry.Handle == Handle;
	});
}

void FMatBP2FPModule::BroadcastNodePhase(const MatBP2FPImportLifecycle::FImportNodePhaseEvent& Event)
{
	MBP_SortAndBroadcastHooks(RegisteredHooks, Event.Phase, Event,
		[](MatBP2FPImportLifecycle::IImportLifecycleHook& Hook,
			const MatBP2FPImportLifecycle::FImportNodePhaseEvent& InEvent)
		{
			Hook.OnNodePhase(InEvent);
		});
}

void FMatBP2FPModule::BroadcastPropertyPhase(const MatBP2FPImportLifecycle::FImportPropertyPhaseEvent& Event)
{
	MBP_SortAndBroadcastHooks(RegisteredHooks, Event.Phase, Event,
		[](MatBP2FPImportLifecycle::IImportLifecycleHook& Hook,
			const MatBP2FPImportLifecycle::FImportPropertyPhaseEvent& InEvent)
		{
			Hook.OnPropertyPhase(InEvent);
		});
}

void FMatBP2FPModule::BroadcastFinalizePhase(const MatBP2FPImportLifecycle::FImportFinalizePhaseEvent& Event)
{
	MBP_SortAndBroadcastHooks(RegisteredHooks, Event.Phase, Event,
		[](MatBP2FPImportLifecycle::IImportLifecycleHook& Hook,
			const MatBP2FPImportLifecycle::FImportFinalizePhaseEvent& InEvent)
		{
			Hook.OnFinalizePhase(InEvent);
		});
}

IMPLEMENT_MODULE(FMatBP2FPModule, MatBP2FP);
