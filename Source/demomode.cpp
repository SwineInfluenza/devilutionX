#include "all.h"

#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>

#include "demomode.h"

#ifdef USE_SDL1
#include "../SourceS/sdl2_to_1_2_backports.h"
#endif

#include "../SourceX/endian_stream.hpp"
#include "../3rdParty/Storm/Source/storm.h"

DEVILUTION_BEGIN_NAMESPACE

// #define LOG_DEMOMODE_MESSAGES
// #define LOG_DEMOMODE_MESSAGES_MOUSEMOTION
// #define LOG_DEMOMODE_MESSAGES_RENDERING
// #define LOG_DEMOMODE_MESSAGES_GAMETICK

void pfile_write_hero_demo(int demo);

HeroCompareResult pfile_compare_hero_demo(int demo, bool logDetails);


namespace {

enum class DemoMsgType : uint8_t {
	GameTick = 0,
	Rendering = 1,
	Message = 2,
};

struct MouseMotionEventData {
	uint16_t x;
	uint16_t y;
};

struct MouseButtonEventData {
	uint8_t button;
	uint16_t x;
	uint16_t y;
	uint16_t mod;
};

struct MouseWheelEventData {
	int32_t x;
	int32_t y;
	uint16_t mod;
};

struct KeyEventData {
	uint32_t sym;
	uint16_t mod;
};

struct DemoMsg {
	DemoMsgType type;
	uint8_t progressToNextGameTick;
	uint32_t eventType;
	union {
		MouseMotionEventData motion;
		MouseButtonEventData button;
		MouseWheelEventData wheel;
		KeyEventData key;
	};
};

int DemoNumber = -1;
bool Timedemo = false;
int RecordNumber = -1;
uint32_t SaveNumber = -1;
bool CreateDemoReference = false;

std::ofstream DemoRecording;
std::deque<DemoMsg> Demo_Message_Queue;
uint32_t DemoModeLastTick = 0;

int LogicTick = 0;
int StartTime = 0;

uint16_t DemoGraphicsWidth = 640;
uint16_t DemoGraphicsHeight = 480;

#if SDL_VERSION_ATLEAST(2, 0, 0)
bool CreateSdlEvent(const DemoMsg &dmsg, SDL_Event &event, SDL_Keymod &modState)
{
	event.type = dmsg.eventType;
	switch (static_cast<SDL_EventType>(dmsg.eventType)) {
	case SDL_MOUSEMOTION:
		event.motion.x = dmsg.motion.x;
		event.motion.y = dmsg.motion.y;
		return true;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		event.button.button = dmsg.button.button;
		event.button.state = dmsg.eventType == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
		event.button.x = dmsg.button.x;
		event.button.y = dmsg.button.y;
		modState = static_cast<SDL_Keymod>(dmsg.button.mod);
		return true;
	case SDL_MOUSEWHEEL:
		event.wheel.x = dmsg.wheel.x;
		event.wheel.y = dmsg.wheel.y;
		modState = static_cast<SDL_Keymod>(dmsg.wheel.mod);
		return true;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		event.key.state = dmsg.eventType == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
		event.key.keysym.sym = dmsg.key.sym;
		event.key.keysym.mod = dmsg.key.mod;
		return true;
	default:
		if (dmsg.eventType >= SDL_USEREVENT) {
			event.type = CustomEventToSdlEvent(dmsg.eventType);
			return true;
		}
		event.type = static_cast<SDL_EventType>(0);
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unsupported demo event (type=%x)", dmsg.eventType);
		return false;
	}
}
#else
SDLKey Sdl2ToSdl1Key(uint32_t key)
{
	if ((key & (1 << 30)) != 0) {
		constexpr uint32_t Keys1Start = 57;
		constexpr SDLKey Keys1[] {
			SDLK_CAPSLOCK, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6,
			SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
			SDLK_PRINTSCREEN, SDLK_SCROLLLOCK, SDLK_PAUSE, SDLK_INSERT, SDLK_HOME,
			SDLK_PAGEUP, SDLK_DELETE, SDLK_END, SDLK_PAGEDOWN, SDLK_RIGHT, SDLK_LEFT,
			SDLK_DOWN, SDLK_UP, SDLK_NUMLOCKCLEAR, SDLK_KP_DIVIDE, SDLK_KP_MULTIPLY,
			SDLK_KP_MINUS, SDLK_KP_PLUS, SDLK_KP_ENTER, SDLK_KP_1, SDLK_KP_2,
			SDLK_KP_3, SDLK_KP_4, SDLK_KP_5, SDLK_KP_6, SDLK_KP_7, SDLK_KP_8,
			SDLK_KP_9, SDLK_KP_0, SDLK_KP_PERIOD
		};
		constexpr uint32_t Keys2Start = 224;
		constexpr SDLKey Keys2[] {
			SDLK_LCTRL, SDLK_LSHIFT, SDLK_LALT, SDLK_LGUI, SDLK_RCTRL, SDLK_RSHIFT,
			SDLK_RALT, SDLK_RGUI, SDLK_MODE
		};
		const uint32_t scancode = key & ~(1 << 30);
		if (scancode >= Keys1Start) {
			if (scancode < Keys1Start + sizeof(Keys1) / sizeof(Keys1[0]))
				return Keys1[scancode - Keys1Start];
			if (scancode >= Keys2Start && scancode < Keys2Start + sizeof(Keys2) / sizeof(Keys2[0]))
				return Keys2[scancode - Keys2Start];
		}
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Demo: unknown key %d", key);
		return SDLK_UNKNOWN;
	}
	if (key <= 122) {
		return static_cast<SDLKey>(key);
	}
	SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Demo: unknown key %d", key);
	return SDLK_UNKNOWN;
}

uint8_t Sdl2ToSdl1MouseButton(uint8_t button)
{
	switch (button) {
	case 4:
		return SDL_BUTTON_X1;
	case 5:
		return SDL_BUTTON_X2;
	default:
		return button;
	}
}

bool CreateSdlEvent(const DemoMsg &dmsg, SDL_Event &event, SDL_Keymod &modState)
{
	switch (dmsg.eventType) {
	case 0x400:
		event.type = SDL_MOUSEMOTION;
		event.motion.x = dmsg.motion.x;
		event.motion.y = dmsg.motion.y;
		return true;
	case 0x401:
	case 0x402:
		event.type = dmsg.eventType == 0x401 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		event.button.which = 0;
		event.button.button = Sdl2ToSdl1MouseButton(dmsg.button.button);
		event.button.state = dmsg.eventType == 0x401 ? SDL_PRESSED : SDL_RELEASED;
		event.button.x = dmsg.button.x;
		event.button.y = dmsg.button.y;
		modState = static_cast<SDL_Keymod>(dmsg.button.mod);
		return true;
	case 0x403: // SDL_MOUSEWHEEL
		if (dmsg.wheel.y == 0) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Demo: unsupported event (mouse wheel y == 0)");
			return false;
		}
		event.type = SDL_MOUSEBUTTONDOWN;
		event.button.button = dmsg.wheel.y > 0 ? SDL_BUTTON_WHEELUP : SDL_BUTTON_WHEELDOWN;
		modState = static_cast<SDL_Keymod>(dmsg.wheel.mod);
		return true;
	case 0x300:
	case 0x301:
		event.type = dmsg.eventType == 0x300 ? SDL_KEYDOWN : SDL_KEYUP;
		event.key.which = 0;
		event.key.state = dmsg.eventType == 0x300 ? SDL_PRESSED : SDL_RELEASED;
		event.key.keysym.sym = Sdl2ToSdl1Key(dmsg.key.sym);
		event.key.keysym.mod = static_cast<SDL_Keymod>(dmsg.key.mod);
		return true;
	default:
		if (dmsg.eventType >= 0x8000) {
			event.type = CustomEventToSdlEvent(dmsg.eventType);
			return true;
		}
		event.type = static_cast<SDL_EventType>(0);
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Demo: unsupported event (type=%x)", dmsg.eventType);
		return false;
	}
}
#endif

void LogDemoMessage(const DemoMsg &msg)
{
#ifdef LOG_DEMOMODE_MESSAGES
	const uint8_t progressToNextGameTick = msg.progressToNextGameTick;
	switch (msg.type) {
	case DemoMsgType::Message: {
		const uint32_t eventType = msg.eventType;
		switch (eventType) {
		case 0x400: // SDL_MOUSEMOTION
#ifdef LOG_DEMOMODE_MESSAGES_MOUSEMOTION
			SDL_Log("🖱️  Message %3d MOUSEMOTION %d %d", progressToNextGameTick,
			    msg.motion.x, msg.motion.y);
#endif
			break;
		case 0x401: // SDL_MOUSEBUTTONDOWN
		case 0x402: // SDL_MOUSEBUTTONUP
			SDL_Log("🖱️  Message %3d %d %d %d %d 0x%x", progressToNextGameTick,
			    eventType == 0x401 ? "MOUSEBUTTONDOWN" : "MOUSEBUTTONUP",
			    msg.button.button, msg.button.x, msg.button.y, msg.button.mod);
			break;
		case 0x403: // SDL_MOUSEWHEEL
			SDL_Log("🖱️  Message %3d MOUSEWHEEL %d %d 0x%x", progressToNextGameTick,
			    msg.wheel.x, msg.wheel.y, msg.wheel.mod);
			break;
		case 0x300: // SDL_KEYDOWN
		case 0x301: // SDL_KEYUP
			SDL_Log("🔤 Message %3d %d 0x%x 0x%x", progressToNextGameTick,
			    eventType == 0x300 ? "KEYDOWN" : "KEYUP",
			    msg.key.sym, msg.key.mod);
			break;
		case 0x100: // SDL_QUIT
			SDL_Log("❎  Message %3d QUIT", progressToNextGameTick);
			break;
		default:
			SDL_Log("📨  Message %3d USEREVENT 0x%x", progressToNextGameTick, eventType);
			break;
		}
	} break;
	case DemoMsgType::GameTick:
#ifdef LOG_DEMOMODE_MESSAGES_GAMETICK
		SDL_Log("⏲️  GameTick %3d", progressToNextGameTick);
#endif
		break;
	case DemoMsgType::Rendering:
#ifdef LOG_DEMOMODE_MESSAGES_RENDERING
		SDL_Log("🖼️  Rendering %3d", progressToNextGameTick);
#endif
		break;
	default:
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "INVALID DEMO MODE MESSAGE %d %3d", static_cast<uint32_t>(msg.type), progressToNextGameTick);
		break;
	}
#endif // LOG_DEMOMODE_MESSAGES
}

bool LoadDemoMessages(int i)
{
	char savePath[MAX_PATH], demoFile[MAX_PATH];

	GetPrefPath(savePath, MAX_PATH);
	snprintf(demoFile, MAX_PATH, "%sdemo_%d.dmo", savePath, i);
	std::ifstream demofile { demoFile, std::fstream::binary };
	if (!demofile.is_open()) {
		return false;
	}

	const uint8_t version = ReadByte(demofile);
	if (version != 0) {
		return false;
	}

	SaveNumber = ReadLE32(demofile);
	if (!(SaveNumber < MAX_CHARACTERS)) { // save file count is limited to 10
		return false;
	}

	DemoGraphicsWidth = ReadLE16(demofile);
	DemoGraphicsHeight = ReadLE16(demofile);

	while (true) {
		const uint32_t typeNum = ReadLE32(demofile);
		if (demofile.eof())
			break;
		const auto type = static_cast<DemoMsgType>(typeNum);

		const uint8_t progressToNextGameTick = ReadByte(demofile);

		switch (type) {
		case DemoMsgType::Message: {
			const uint32_t eventType = ReadLE32(demofile);
			DemoMsg msg { type, progressToNextGameTick, eventType, {} };
			switch (eventType) {
			case 0x400: // SDL_MOUSEMOTION
				msg.motion.x = ReadLE16(demofile);
				msg.motion.y = ReadLE16(demofile);
				break;
			case 0x401: // SDL_MOUSEBUTTONDOWN
			case 0x402: // SDL_MOUSEBUTTONUP
				msg.button.button = ReadByte(demofile);
				msg.button.x = ReadLE16(demofile);
				msg.button.y = ReadLE16(demofile);
				msg.button.mod = ReadLE16(demofile);
				break;
			case 0x403: // SDL_MOUSEWHEEL
				msg.wheel.x = ReadLE32<int32_t>(demofile);
				msg.wheel.y = ReadLE32<int32_t>(demofile);
				msg.wheel.mod = ReadLE16(demofile);
				break;
			case 0x300: // SDL_KEYDOWN
			case 0x301: // SDL_KEYUP
				msg.key.sym = static_cast<SDL_Keycode>(ReadLE32(demofile));
				msg.key.mod = static_cast<SDL_Keymod>(ReadLE16(demofile));
				break;
			case 0x100: // SDL_QUIT
				break;
			default:
				if (eventType < 0x8000) { // SDL_USEREVENT
					app_fatal("Unknown event %x", eventType);
				}
				break;
			}
			Demo_Message_Queue.push_back(msg);
			break;
		}
		default:
			Demo_Message_Queue.push_back(DemoMsg { type, progressToNextGameTick, 0, {} });
			break;
		}
	}

	demofile.close();

	DemoModeLastTick = SDL_GetTicks();

	return true;
}

void RecordEventHeader(const SDL_Event &event)
{
	WriteLE32(DemoRecording, static_cast<uint32_t>(DemoMsgType::Message));
	WriteByte(DemoRecording, gbProgressToNextGameTick);
	WriteLE32(DemoRecording, event.type);
}

} // namespace

namespace demo {

void InitPlayBack(int demoNumber, bool timedemo)
{
	DemoNumber = demoNumber;
	Timedemo = timedemo;
	sgbControllerActive = false; // ControlMode = ControlTypes::KeyboardAndMouse;

	if (!LoadDemoMessages(demoNumber)) {
		SDL_Log("Unable to load demo file");
		exit(1);
	}
}
void InitRecording(int recordNumber, bool createDemoReference)
{
	RecordNumber = recordNumber;
	CreateDemoReference = createDemoReference;
}
/*
void OverrideOptions()
{
#ifndef USE_SDL1
	sgOptions.Graphics.fitToScreen.SetValue(false);
#endif
#if SDL_VERSION_ATLEAST(2, 0, 0)
	sgOptions.Graphics.hardwareCursor.SetValue(false);
#endif
	if (Timedemo) {
#ifndef USE_SDL1
		sgOptions.Graphics.vSync.SetValue(false);
#endif
		sgOptions.Graphics.limitFPS.SetValue(false);
	}
	forceResolution = Size(DemoGraphicsWidth, DemoGraphicsHeight);
}
*/
bool IsRunning()
{
	return DemoNumber != -1;
}

bool IsRecording()
{
	return RecordNumber != -1;
}

bool GetRunGameLoop(bool &drawGame, bool &processInput)
{
	if (Demo_Message_Queue.empty())
		app_fatal("Demo queue empty");
	const DemoMsg dmsg = Demo_Message_Queue.front();
	LogDemoMessage(dmsg);
	if (dmsg.type == DemoMsgType::Message)
		app_fatal("Unexpected Message");
	if (Timedemo) {
		// disable additonal rendering to speedup replay
		drawGame = dmsg.type == DemoMsgType::GameTick;
	} else {
		int currentTickCount = SDL_GetTicks();
		int ticksElapsed = currentTickCount - DemoModeLastTick;
		bool tickDue = ticksElapsed >= gnTickDelay;
		drawGame = false;
		if (tickDue) {
			if (dmsg.type == DemoMsgType::GameTick) {
				DemoModeLastTick = currentTickCount;
			}
		} else {
			int32_t fraction = ticksElapsed * AnimationScalingFactor / gnTickDelay;
			fraction = std::min<int32_t>(std::max(fraction, 0), AnimationScalingFactor); // std::clamp
			uint8_t progressToNextGameTick = static_cast<uint8_t>(fraction);
			if (dmsg.type == DemoMsgType::GameTick || dmsg.progressToNextGameTick > progressToNextGameTick) {
				// we are ahead of the replay => add a additional rendering for smoothness
				if (gbRunGame && PauseMode == 0 && (gbMaxPlayers > 1 || !gmenu_is_active()) && gbProcessPlayers) // if game is not running or paused there is no next gametick in the near future
					gbProgressToNextGameTick = progressToNextGameTick;
				processInput = false;
				drawGame = true;
				return false;
			}
		}
	}
	gbProgressToNextGameTick = dmsg.progressToNextGameTick;
	Demo_Message_Queue.pop_front();
	if (dmsg.type == DemoMsgType::GameTick)
		LogicTick++;
	return dmsg.type == DemoMsgType::GameTick;
}

bool FetchMessage(SDL_Event &event, SDL_Keymod &modState)
{
	if (CurrentProc == DisableInputWndProc)
		return false;

	SDL_Event e;
	if (SDL_PollEvent(&e) != 0) {
		if (e.type == SDL_QUIT) {
			event = e;
			return true;
		}
		if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
			Demo_Message_Queue.clear();
//			ClearMessageQueue();
			DemoNumber = -1;
			Timedemo = false;
			last_tick = SDL_GetTicks();
			sgGameInitInfo.nTickRate = GameBasicTickRate;
		}
		if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_KP_PLUS || e.key.keysym.sym == SDLK_PLUS) && sgGameInitInfo.nTickRate < 255) {
			sgGameInitInfo.nTickRate++;
		}
		if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_KP_MINUS || e.key.keysym.sym == SDLK_MINUS) && sgGameInitInfo.nTickRate > 1) {
			sgGameInitInfo.nTickRate--;
		}
		gnTickDelay = 1000 / sgGameInitInfo.nTickRate;
	}

	if (!Demo_Message_Queue.empty()) {
		const DemoMsg dmsg = Demo_Message_Queue.front();
		LogDemoMessage(dmsg);
		if (dmsg.type == DemoMsgType::Message) {
			const bool hasEvent = CreateSdlEvent(dmsg, event, modState);
			gbProgressToNextGameTick = dmsg.progressToNextGameTick;
			Demo_Message_Queue.pop_front();
			return hasEvent;
		}
	}

	return false;
}

void RecordGameLoopResult(bool runGameLoop)
{
	WriteLE32(DemoRecording, static_cast<uint32_t>(runGameLoop ? DemoMsgType::GameTick : DemoMsgType::Rendering));
	WriteByte(DemoRecording, gbProgressToNextGameTick);
}

void RecordMessage(const SDL_Event &event, uint16_t modState)
{
	if (!gbRunGame || !DemoRecording.is_open())
		return;
	if (CurrentProc == DisableInputWndProc)
		return;

	switch (event.type) {
	case SDL_MOUSEMOTION:
		RecordEventHeader(event);
		WriteLE16(DemoRecording, event.motion.x);
		WriteLE16(DemoRecording, event.motion.y);
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		RecordEventHeader(event);
		WriteByte(DemoRecording, event.button.button);
		WriteLE16(DemoRecording, event.button.x);
		WriteLE16(DemoRecording, event.button.y);
		WriteLE16(DemoRecording, modState);
		break;
#ifndef USE_SDL1
	case SDL_MOUSEWHEEL:
		RecordEventHeader(event);
		WriteLE32(DemoRecording, event.wheel.x);
		WriteLE32(DemoRecording, event.wheel.y);
		WriteLE16(DemoRecording, modState);
		break;
#endif
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		RecordEventHeader(event);
		WriteLE32(DemoRecording, static_cast<uint32_t>(event.key.keysym.sym));
		WriteLE16(DemoRecording, static_cast<uint16_t>(event.key.keysym.mod));
		break;
#ifndef USE_SDL1
	case SDL_WINDOWEVENT:
		if (event.window.type == SDL_WINDOWEVENT_CLOSE) {
			SDL_Event quitEvent;
			quitEvent.type = SDL_QUIT;
			RecordEventHeader(quitEvent);
		}
		break;
#endif
	case SDL_QUIT:
		RecordEventHeader(event);
		break;
	default:
		if (IsCustomEvent(event.type)) {
			SDL_Event stableCustomEvent;
			stableCustomEvent.type = SDL_USEREVENT + static_cast<uint32_t>(GetCustomEvent(event.type));
			RecordEventHeader(stableCustomEvent);
		}
		break;
	}
}

void NotifyGameLoopStart()
{
	if (IsRecording()) {
		char savePath[MAX_PATH], demoFile[MAX_PATH];

		GetPrefPath(savePath, MAX_PATH);
		snprintf(demoFile, MAX_PATH, "%sdemo_%d.dmo", savePath, RecordNumber);
		DemoRecording.open(demoFile, std::fstream::trunc | std::fstream::binary);

		constexpr uint8_t Version = 0;
		WriteByte(DemoRecording, Version);
		WriteLE32(DemoRecording, pfile_get_save_num_from_name(plr[myplr]._pName)); // gSaveNumber
		WriteLE16(DemoRecording, SCREEN_WIDTH);
		WriteLE16(DemoRecording, SCREEN_HEIGHT);
	}

	if (IsRunning()) {
		StartTime = SDL_GetTicks();
		LogicTick = 0;
	}
}

void NotifyGameLoopEnd()
{
	if (IsRecording()) {
		DemoRecording.close();
		if (CreateDemoReference)
			pfile_write_hero_demo(RecordNumber);

		RecordNumber = -1;
		CreateDemoReference = false;
	}

	if (IsRunning()) {
		float seconds = (SDL_GetTicks() - StartTime) / 1000.0f;
		SDL_Log("%d frames, %.2f seconds: %.1f fps", LogicTick, seconds, LogicTick / seconds);
		gbRunGameResult = false;
		gbRunGame = false;

		HeroCompareResult compareResult = pfile_compare_hero_demo(DemoNumber, false);
		switch (compareResult.status) {
		case HeroCompareResult::ReferenceNotFound:
			SDL_Log("Timedemo: No final comparison cause reference is not present.");
			break;
		case HeroCompareResult::Same:
			SDL_Log("Timedemo: Same outcome as initial run. :)");
			break;
		case HeroCompareResult::Difference:
			SDL_Log("Timedemo: Different outcome than initial run. ;(\n%s", compareResult.message.c_str());
			break;
		}
	}
}

BOOL GetHeroInfoCallback(_uiheroinfo *pInfo)
{
	if (pfile_get_save_num_from_name(pInfo->name) == SaveNumber)
		SStrCopy(gszHero, pInfo->name, sizeof(gszHero));

	return TRUE;
}

} // namespace demo

DEVILUTION_END_NAMESPACE // namespace devilution
