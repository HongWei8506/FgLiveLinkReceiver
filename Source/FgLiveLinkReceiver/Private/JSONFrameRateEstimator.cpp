// Copyright 2023 FACEGOOD, Inc. All Rights Reserved.

#include "JSONFrameRateEstimator.h"

void FJSONFrameRateEstimator::Reset()
{
	WindowStartFrameId = INDEX_NONE;
	WindowStartTimeSeconds = 0.0;
	LastFrameId = INDEX_NONE;
	WindowSampleCount = 0;
	EstimatedFramesPerSecond = 60.0;
	bHasEstimate = false;
}

void FJSONFrameRateEstimator::AddFrame(
	const int32 FrameId,
	const double ArrivalTimeSeconds)
{
	if (FrameId == INDEX_NONE || !FMath::IsFinite(ArrivalTimeSeconds))
	{
		return;
	}

	if (LastFrameId != INDEX_NONE && FrameId <= LastFrameId)
	{
		if (static_cast<int64>(LastFrameId) - FrameId > RestartBackwardFrameThreshold)
		{
			Reset();
		}
		else
		{
			return;
		}
	}

	if (WindowStartFrameId == INDEX_NONE)
	{
		WindowStartFrameId = FrameId;
		WindowStartTimeSeconds = ArrivalTimeSeconds;
		LastFrameId = FrameId;
		WindowSampleCount = 1;
		return;
	}

	LastFrameId = FrameId;
	++WindowSampleCount;

	const double ElapsedSeconds = ArrivalTimeSeconds - WindowStartTimeSeconds;
	const int64 DeltaFrames = static_cast<int64>(FrameId) - WindowStartFrameId;
	if (WindowSampleCount < MinimumSamplesPerWindow
		|| ElapsedSeconds < MinimumWindowSeconds
		|| DeltaFrames <= 0)
	{
		return;
	}

	const double MeasuredFramesPerSecond =
		static_cast<double>(DeltaFrames) / ElapsedSeconds;
	if (FMath::IsFinite(MeasuredFramesPerSecond)
		&& MeasuredFramesPerSecond >= 1.0
		&& MeasuredFramesPerSecond <= 240.0)
	{
		if (bHasEstimate)
		{
			constexpr double SmoothingFactor = 0.35;
			EstimatedFramesPerSecond = FMath::Lerp(
				EstimatedFramesPerSecond,
				MeasuredFramesPerSecond,
				SmoothingFactor);
		}
		else
		{
			EstimatedFramesPerSecond = MeasuredFramesPerSecond;
		}
		bHasEstimate = true;
	}

	WindowStartFrameId = FrameId;
	WindowStartTimeSeconds = ArrivalTimeSeconds;
	WindowSampleCount = 1;
}

double FJSONFrameRateEstimator::GetFramesPerSecond() const
{
	return FMath::Clamp(EstimatedFramesPerSecond, 1.0, 240.0);
}

FFrameRate FJSONFrameRateEstimator::GetSceneFrameRate() const
{
	return FFrameRate(NominalSceneFramesPerSecond, 1);
}
