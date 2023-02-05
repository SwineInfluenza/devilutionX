/**
 * @file demomode.h
 *
 * Contains most of the the demomode specific logic
 */
#pragma once

#include <SDL.h>

#include <string>

DEVILUTION_BEGIN_NAMESPACE

struct HeroCompareResult {
	enum Status : uint8_t {
		ReferenceNotFound,
		Same,
		Difference,
	};
	Status status;
	std::string message;
};

constexpr uint8_t GameBasicTickRate = 20;
constexpr uint8_t AnimationScalingFactor = 128; // AnimationInfo::baseValueFraction

namespace demo {

void InitPlayBack(int demoNumber, bool timedemo);
void InitRecording(int recordNumber, bool createDemoReference);
//void OverrideOptions();

bool IsRunning();
bool IsRecording();

bool GetRunGameLoop(bool &drawGame, bool &processInput);
bool FetchMessage(SDL_Event &event, SDL_Keymod &modState);
void RecordGameLoopResult(bool runGameLoop);
void RecordMessage(const SDL_Event &event, uint16_t modState);

void NotifyGameLoopStart();
void NotifyGameLoopEnd();

BOOL GetHeroInfoCallback(_uiheroinfo *pInfo);

} // namespace demo

DEVILUTION_END_NAMESPACE // namespace devilution
