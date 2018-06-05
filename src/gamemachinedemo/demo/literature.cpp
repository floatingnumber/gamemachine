﻿#include "stdafx.h"
#include "literature.h"
#include <linearmath.h>
#include <gmm.h>

void Demo_Literature::init()
{
	D(d);
	D_BASE(db, Base);
	Base::init();

	GM_ASSERT(!getDemoWorldReference());
	getDemoWorldReference() = new gm::GMDemoGameWorld(d->parentDemonstrationWorld->getContext());

	// 读取边框
	gm::GMGamePackage* package = GM.getGamePackageManager();
	gm::GMBuffer buf;
	bool b = package->readFile(gm::GMPackageIndex::Textures, "frame.png", &buf);
	GM_ASSERT(b);

	gm::GMImage* img = nullptr;
	gm::GMImageReader::load(buf.buffer, buf.size, &img);
	gm::ITexture* frameTexture = nullptr;
	GM.getFactory()->createTexture(db->parentDemonstrationWorld->getContext(), img, &frameTexture);
	GM_ASSERT(frameTexture);

	gm::GMAsset border = getDemoWorldReference()->getAssets().insertAsset(gm::GMAssetType::Texture, frameTexture);
	gm::GMRect textureGeo = { 0, 0, 308, 94 }; //截取的纹理位置


	gm::GMTextGameObject* literature = new gm::GMTextGameObject(getDemoWorldReference()->getContext()->getWindow()->getRenderRect());
	gm::GMRect rect = { 200, 220, 400, 190 };
	literature->setGeometry(rect);
	literature->setText(
		"This is a [color=#ffbbff]text[color=#ffffff] demo. It shows you how to render ABC,xyz,etc.[n]"
		"[color=#ff0000]red[n]"
		"[color=#00ff00]green[n]"
		"[color=#0000ff]blue[n]"
		"[color=#ff00FF]magenta[n][color=#ffffff]"
		"[size=20]20Points Font Size[n]"
		"[size=10]10Points Font Size[n]"
		"[n]"
		"[size=25][n]Let's try some 'overflow'"
	);

	gm::GMDemoGameWorld* world = gm::gm_cast<gm::GMDemoGameWorld*>(getDemoWorldReference());
	world->addObject("text", literature);

	GM_delete(img);
}

void Demo_Literature::event(gm::GameMachineHandlerEvent evt)
{
	D(d);
	Base::event(evt);
	switch (evt)
	{
	case gm::GameMachineHandlerEvent::FrameStart:
		break;
	case gm::GameMachineHandlerEvent::FrameEnd:
		break;
	case gm::GameMachineHandlerEvent::Simulate:
		getDemoWorldReference()->simulateGameWorld();
		break;
	case gm::GameMachineHandlerEvent::Render:
		getDemoWorldReference()->renderScene();
		break;
	case gm::GameMachineHandlerEvent::Activate:
		break;
	case gm::GameMachineHandlerEvent::Deactivate:
		break;
	case gm::GameMachineHandlerEvent::Terminate:
		break;
	default:
		break;
	}
}