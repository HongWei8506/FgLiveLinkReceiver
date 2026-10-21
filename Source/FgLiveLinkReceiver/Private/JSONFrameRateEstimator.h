// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"

/**
 * Estimates the effective source frame rate from FrameId progression and
 * packet arrival time. This is an estimate because the wire protocol does not
 * currently contain the sender's authoritative frame rate.
 */
class FJSONFrameRateEstimator
{
public:
	static constexpr int32 MinimumSamplesPerWindow = 6;
	static constexpr double MinimumWindowSeconds = 0.2;
	static constexpr int32 RestartBackwardFrameThreshold = 600;
	static constexpr int32 NominalSceneFramesPerSecond = 30;

	void Reset();

	void AddFrame(
		int32 FrameId,
		double ArrivalTimeSeconds);

	bool HasEstimate() const { return bHasEstimate; }

	double GetFramesPerSecond() const;

	FFrameRate GetSceneFrameRate() const;

private:
	int32 WindowStartFrameId = INDEX_NONE;
	double WindowStartTimeSeconds = 0.0;
	int32 LastFrameId = INDEX_NONE;
	int32 WindowSampleCount = 0;
	double EstimatedFramesPerSecond = 60.0;
	bool bHasEstimate = false;
};
