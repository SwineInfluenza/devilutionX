//HEADER_GOES_HERE
#ifndef __INTERFAC_H__
#define __INTERFAC_H__

extern int progress_id;

void interface_msg_pump();
BOOL IncProgress();
void DrawCutscene();
void DrawProgress(int screen_x, int screen_y, int progress_id);
void ShowProgress(unsigned int uMsg);
void FreeInterface();
void InitCutscene(unsigned int uMsg);

void RegisterCustomEvents();
bool IsCustomEvent(uint32_t eventType);
uint32_t GetCustomEvent(uint32_t eventType);
interface_mode GetCustomMessage(uint32_t eventType);
uint32_t CustomEventToSdlEvent(uint32_t eventType);
uint32_t CustomMessageToSdlEvent(interface_mode messageType);

/* rdata */

extern const BYTE BarColor[3];
extern const int BarPos[3][2];

#endif /* __INTERFAC_H__ */
