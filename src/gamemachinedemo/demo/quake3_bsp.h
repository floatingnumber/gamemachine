﻿#ifndef __DEMO_QUAKE3_BSP_H__
#define __DEMO_QUAKE3_BSP_H__

#include <gamemachine.h>
#include <gmdemogameworld.h>
#include "demonstration_world.h"

class GMBSPGameWorld;
GM_PRIVATE_OBJECT(Demo_Quake3_BSP)
{
	gm::GMBSPGameWorld* world = nullptr;
	gm::GMSpriteGameObject* sprite = nullptr;
	bool mouseTrace = false;
};

class Demo_Quake3_BSP : public DemoHandler
{
	DECLARE_PRIVATE_AND_BASE(Demo_Quake3_BSP, DemoHandler)

public:
	using Base::Base;
	~Demo_Quake3_BSP();

public:
	virtual void init() override;
	virtual void event(gm::GameMachineEvent evt) override;
	virtual void onActivate() override;
	virtual void onDeactivate() override;

protected:
	virtual void setLookAt() override;
	virtual void setDefaultLights() override;

private:
	void setMouseTrace(bool enabled);
};

#endif