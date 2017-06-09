﻿#ifndef __DIRECTSOUND_SOUNDPLAYER_H__
#define __DIRECTSOUND_SOUNDPLAYER_H__
#include "common.h"
#include "foundation/utilities/utilities.h"
#include "gmdatacore/soundreader/gmsoundreader.h"

#if _WINDOWS
#include <dsound.h>
#include <mmsystem.h>
#endif

BEGIN_NS

#if _WINDOWS

class GMUIWindow;
class GMSoundPlayerDevice
{
public:
	static void createInstance(GMUIWindow* window);
	static IDirectSound8* getInstance();

private:
	GMSoundPlayerDevice(GMUIWindow* window);

private:
	ComPtr<IDirectSound8> m_cpDirectSound;
};

#endif

END_NS
#endif
