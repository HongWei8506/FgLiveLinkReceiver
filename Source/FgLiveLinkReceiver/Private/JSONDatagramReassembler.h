// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Reassembles JSON objects that are concatenated or split across UDP datagrams.
 * No sequence number is required, but split fragments must arrive in order.
 * ACK, retransmission, and recovery of lost fragments require sender support.
 */
class FJSONDatagramReassembler
{
public:
	static constexpr double PartialMessageTimeoutSeconds = 2.0;
	static constexpr int32 MaxPendingBytes = 4 * 1024 * 1024;

	void AddPacket(
		const FString& PeerAddress,
		const uint8* Data,
		int32 NumBytes,
		double CurrentTimeSeconds,
		TArray<TArray<uint8>>& OutCompleteMessages);

	void Reset();

private:
	struct FPeerState
	{
		TArray<uint8> PendingBytes;
		double LastPacketTimeSeconds = 0.0;
	};

	TMap<FString, FPeerState> PeerStates;
};
