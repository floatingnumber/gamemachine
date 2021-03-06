﻿#include "stdafx.h"
#include "gmgraphicengine.h"
#include "gmgraphicengine_p.h"
#include "foundation/utilities/utilities.h"
#include "gmassets.h"
#include "foundation/gamemachine.h"
#include "gmengine/gameobjects/gmgameobject.h"
#include "foundation/gmprofile.h"
#include "foundation/gmconfigs.h"
#include "gmprimitivemanager.h"
#include "gmcsmhelper.h"
#include "gmcomputeshadermanager.h"
#include <algorithm>

BEGIN_NS

static GMShaderVariablesDesc s_defaultShaderVariablesDesc =
{
	"GM_WorldMatrix",
	"GM_ViewMatrix",
	"GM_ProjectionMatrix",
	"GM_InverseTransposeModelMatrix",
	"GM_InverseViewMatrix",

	"GM_Bones",
	"GM_UseAnimation",

	"GM_ViewPosition",

	{ "OffsetX", "OffsetY", "ScaleX", "ScaleY", "Enabled", "Texture" },
	"GM_AmbientTextureAttribute",
	"GM_DiffuseTextureAttribute",
	"GM_SpecularTextureAttribute",
	"GM_NormalMapTextureAttribute",
	"GM_LightmapTextureAttribute",
	"GM_AlbedoTextureAttribute",
	"GM_MetallicRoughnessAOTextureAttribute",
	"GM_CubeMapTextureAttribute",

	"GM_LightCount",

	{ "Ka", "Kd", "Ks", "Shininess", "Refractivity", "F0" },
	"GM_Material",

	{
		"GM_Filter",
		"GM_KernelDeltaX",
		"GM_KernelDeltaY",
		"GM_BlendFactor",
		{
			"GM_DefaultFilter",
			"GM_InversionFilter",
			"GM_SharpenFilter",
			"GM_BlurFilter",
			"GM_GrayscaleFilter",
			"GM_EdgeDetectFilter",
			"GM_BlendFilter",
		}
	},

	{
		"GM_ScreenInfo",
		"ScreenWidth",
		"ScreenHeight",
		"Multisampling",
	},

	"GM_RasterizerState",
	"GM_BlendState",
	"GM_DepthStencilState",

	{
		"GM_ShadowInfo",
		"HasShadow",
		"ShadowMatrix",
		"EndClip",
		"CurrentCascadeLevel",
		"Position",
		"GM_ShadowMap",
		"GM_ShadowMapMSAA",
		"ShadowMapWidth",
		"ShadowMapHeight",
		"BiasMin",
		"BiasMax",
		"CascadedShadowLevel",
		"ViewCascade",
		"PCFRows",
	},

	{
		"GM_GammaCorrection",
		"GM_Gamma",
		"GM_GammaInv",
	},

	{
		"GM_HDR",
		"GM_ToneMapping",
	},

	"GM_IlluminationModel",
	"GM_ColorVertexOp",

	{
		"GM_Debug_Normal",
	}
};

GMint64 GMShadowSourceDesc::version = 0;

GM_PRIVATE_OBJECT_UNALIGNED(GMFramebuffersStack)
{
	Stack<IFramebuffers*> framebuffers;
};

GMFramebuffersStack::GMFramebuffersStack()
{
	GM_CREATE_DATA();
}

GMFramebuffersStack::~GMFramebuffersStack()
{

}

void GMFramebuffersStack::push(IFramebuffers* framebuffers)
{
	D(d);
	d->framebuffers.push(framebuffers);
}

IFramebuffers* GMFramebuffersStack::pop()
{
	D(d);
	if (d->framebuffers.empty())
		return nullptr;

	IFramebuffers* framebuffers = d->framebuffers.top();
	d->framebuffers.pop();
	return framebuffers;
}

IFramebuffers* GMFramebuffersStack::peek()
{
	D(d);
	if (d->framebuffers.empty())
		return nullptr;
	return d->framebuffers.top();
}

void GMGraphicEnginePrivate::setCascadeCamera(GMCascadeLevel level, const GMCamera& camera)
{
	shadowCameraVPmatrices[level] = camera.getViewMatrix() * camera.getProjectionMatrix();
}

void GMGraphicEnginePrivate::deleteLights()
{
	for (auto light : lights)
	{
		light->destroy();
	}

	lights.clear();
}

void GMGraphicEnginePrivate::dispose()
{
	deleteLights();
	GMComputeShaderManager::instance().disposeShaderPrograms(context);
	if (filterFramebuffers)
		filterFramebuffers->destroy();
	if (filterQuad)
		filterQuad->destroy();
	if (gBuffer)
		gBuffer->destroy();
	if (shadowDepthFramebuffers)
		shadowDepthFramebuffers->destroy();
	if (defaultFramebuffers)
		defaultFramebuffers->destroy();
	if (glyphManager)
		glyphManager->destroy();
}

IGBuffer* GMGraphicEnginePrivate::createGBuffer()
{
	IGBuffer* gBuffer = nullptr;
	GM.getFactory()->createGBuffer(context, &gBuffer);
	GM_ASSERT(gBuffer);
	return gBuffer;
}

GMGameObject* GMGraphicEngine::getFilterQuad()
{
	D(d);
	return d->filterQuad;
}

IShaderLoadCallback* GMGraphicEngine::getShaderLoadCallback()
{
	D(d);
	return d->shaderLoadCallback;
}

const Vector<ILight*>& GMGraphicEngine::getLights()
{
	D(d);
	return d->lights;
}

GMGraphicEngine::GMGraphicEngine(const IRenderContext* context)
{
	GM_CREATE_DATA();

	D(d);
	d->mtid = GMThread::getCurrentThreadId();
	d->context = context;
	d->renderConfig = d->configs.getConfig(GMConfigs::Render).asRenderConfig();
	d->debugConfig = d->configs.getConfig(GMConfigs::Debug).asDebugConfig();
	d->shadow.type = GMShadowSourceDesc::NoShadow;
	d->renderTechniqueManager.reset(new GMRenderTechniqueManager(context));

	if (context->getWindow())
	{
		d->primitiveManager.reset(new GMPrimitiveManager(context));
	}
	else
	{
		gm_info(gm_dbg_wrap("You are in a compute only mode, so you cannot use primitive mananger because there's no render window."));
	}
}

GMGraphicEngine::~GMGraphicEngine()
{
	if (GM_HAS_DATA())
	{
		D(d);
		d->dispose();
	}
}

void GMGraphicEngine::init()
{
	D(d);
	getDefaultFramebuffers()->bind();
}

IGBuffer* GMGraphicEngine::getGBuffer()
{
	D(d);
	if (!d->gBuffer)
	{
		d->gBuffer = d->createGBuffer();
		d->gBuffer->init();
	}
	return d->gBuffer;
}

IFramebuffers* GMGraphicEngine::getFilterFramebuffers()
{
	D(d);
	return d->filterFramebuffers;
}

void GMGraphicEngine::begin()
{
	D(d);
	++d->begun;

	// 是否使用滤镜
	bool useFilterFramebuffer = needUseFilterFramebuffer();
	if (useFilterFramebuffer)
	{
		createFilterFramebuffer();
		clearFilterFramebuffer();
	}
}

void GMGraphicEngine::draw(const GMGameObjectContainer& forwardRenderingObjects, const GMGameObjectContainer& deferredRenderingObjects)
{
	GM_PROFILE(this, "draw");
	D(d);
	if (!d->begun)
	{
		gm_warning(gm_dbg_wrap("You should call IGraphicEngine::begin before call IGraphicEngine::draw."));
	}

	// 如果绘制阴影，先生成阴影缓存
	if (d->shadow.type != GMShadowSourceDesc::NoShadow)
	{
		generateShadowBuffer(forwardRenderingObjects, deferredRenderingObjects);
		d->lastShadow = d->shadow;
	}

	// 绘制需要延迟渲染的对象
	bool useFilterFramebuffer = needUseFilterFramebuffer();
	if (!deferredRenderingObjects.empty())
	{
		IGBuffer* gBuffer = getGBuffer();
		gBuffer->geometryPass(deferredRenderingObjects);

		if (useFilterFramebuffer)
			bindFilterFramebuffer();

		gBuffer->lightPass();
		if (useFilterFramebuffer)
		{
			unbindFilterFramebuffer();
			gBuffer->getGeometryFramebuffers()->copyDepthStencilFramebuffer(getFilterFramebuffers());
		}
		else
		{
			gBuffer->getGeometryFramebuffers()->copyDepthStencilFramebuffer(getDefaultFramebuffers());
		}
	}

	// 绘制不需要延迟渲染的对象
	if (!forwardRenderingObjects.empty())
	{
		if (useFilterFramebuffer)
			bindFilterFramebuffer();

		draw(forwardRenderingObjects);

		if (useFilterFramebuffer)
			unbindFilterFramebuffer();
	}
}

void GMGraphicEngine::draw(const GMGameObjectContainer& objects)
{
	D(d);
	for (auto object : objects)
	{
		object->draw();
	}
}

void GMGraphicEngine::end()
{
	D(d);
	--d->begun;

	if (d->begun < 0)
	{
		d->begun = 0;
		gm_warning(gm_dbg_wrap("Too many 'end()' have been invoked."));
	}

	if (!d->begun)
	{
		bool useFilterFramebuffer = needUseFilterFramebuffer();
		if (useFilterFramebuffer)
			drawFilterFramebuffer();
	}
}

const GMFilterMode::Mode GMGraphicEngine::getCurrentFilterMode()
{
	D(d);
	return d->renderConfig.get(GMRenderConfigs::FilterMode).toEnum<GMFilterMode::Mode>();
}

const GMVec3 GMGraphicEngine::getCurrentFilterBlendFactor()
{
	D(d);
	return d->renderConfig.get(GMRenderConfigs::BlendFactor_Vec3).toVec3();
}

IFramebuffers* GMGraphicEngine::getShadowMapFramebuffers()
{
	D(d);
	return d->shadowDepthFramebuffers;
}

bool GMGraphicEngine::needGammaCorrection()
{
	D(d);
	return d->renderConfig.get(GMRenderConfigs::GammaCorrection_Bool).toBool();
}

GMfloat GMGraphicEngine::getGammaValue()
{
	D(d);
	return d->renderConfig.get(GMRenderConfigs::Gamma_Float).toFloat();
}

bool GMGraphicEngine::needHDR()
{
	D(d);
	return d->renderConfig.get(GMRenderConfigs::HDR_Bool).toBool();
}

GMToneMapping::Mode GMGraphicEngine::getToneMapping()
{
	D(d);
	return d->renderConfig.get(GMRenderConfigs::ToneMapping).toInt();
}

bool GMGraphicEngine::isWireFrameMode(GMModel* model)
{
	D(d);
	// 有几种类型的Model不会绘制线框图
	auto type = model->getType();
	if (type == GMModelType::Model2D ||
		type == GMModelType::Text)
	{
		return false;
	}

	return d->debugConfig.get(GMDebugConfigs::WireFrameMode_Bool).toBool();
}

bool GMGraphicEngine::isNeedDiscardTexture(GMModel* model, GMTextureType type)
{
	D(d);
	return type != GMTextureType::Lightmap &&
		d->debugConfig.get(GMDebugConfigs::DrawLightmapOnly_Bool).toBool() &&
		model->getType() != GMModelType::Model2D &&
		model->getType() != GMModelType::Text;
}

ICSMFramebuffers* GMGraphicEngine::getCSMFramebuffers()
{
	ICSMFramebuffers* csm = nullptr;
	getInterface(GameMachineInterfaceID::CSMFramebuffer, (void**)&csm);
	return csm;
}

void GMGraphicEngine::createShadowFramebuffers(OUT IFramebuffers** framebuffers)
{
	D(d);
	GM_ASSERT(framebuffers);

	IFramebuffers* sdframebuffers = nullptr;
	GM.getFactory()->createShadowFramebuffers(d->context, &sdframebuffers);
	GM_ASSERT(sdframebuffers);

	(*framebuffers) = sdframebuffers;

	GMFramebuffersDesc desc;
	GMRect rect = { 0 };
	// 构造一个 (width * cascadedShadowLevel, height) 的shadow map
	rect.width = d->shadow.width * d->shadow.cascades;
	rect.height = d->shadow.height;
	desc.rect = rect;

	getCSMFramebuffers()->setShadowSource(d->shadow);

	bool succeed = sdframebuffers->init(desc);
	GM_ASSERT(succeed);
}

void GMGraphicEngine::resetCSM()
{
	D(d);
	ICSMFramebuffers* csm = getCSMFramebuffers();
	const GMShadowSourceDesc& shadowSourceDesc = getShadowSourceDesc();
	if (shadowSourceDesc.cascades > 1)
	{
		for (GMCascadeLevel i = 0; i < shadowSourceDesc.cascades; ++i)
		{
			csm->applyCascadedLevel(i);

			// 我们需要计算出此层的投影和frustum
			GMCamera shadowCamera = shadowSourceDesc.camera;
			GMCSMHelper::setOrthoCamera(csm, getCamera(), shadowSourceDesc, shadowCamera);
			d->setCascadeCamera(i, shadowCamera);
		}
	}
	else
	{
		// 如果只有一层，则不使用CSM
		d->setCascadeCamera(0, shadowSourceDesc.camera);
	}
}

void GMGraphicEngine::createFilterFramebuffer()
{
	D(d);
	if (!d->filterFramebuffers)
	{
		IFactory* factory = GM.getFactory();
		const GMWindowStates& windowStates = d->context->getWindow()->getWindowStates();
		GMFramebufferDesc desc = { 0 };
		desc.rect = windowStates.renderRect;
		desc.framebufferFormat = GMFramebufferFormat::R32G32B32A32_FLOAT;
		factory->createFramebuffers(d->context, &d->filterFramebuffers);
		GM_ASSERT(d->filterFramebuffers);
		GMFramebuffersDesc fbDesc;
		fbDesc.rect = windowStates.renderRect;
		d->filterFramebuffers->init(fbDesc);
		IFramebuffer* framebuffer = nullptr;
		factory->createFramebuffer(d->context, &framebuffer);
		GM_ASSERT(framebuffer);
		framebuffer->init(desc);
		d->filterFramebuffers->addFramebuffer(framebuffer);
	}

	if (!d->filterQuad)
	{
		GMModelAsset quad;
		GMPrimitiveCreator::createQuadrangle(GMPrimitiveCreator::one2(), 0, quad);
		GM_ASSERT(!quad.isEmpty());
		GMModel* quadModel = quad.getScene()->getModels()[0].getModel();
		quadModel->setType(GMModelType::Filter);

		GMTextureAsset texture;
		d->filterFramebuffers->getFramebuffer(0)->getTexture(texture);
		quadModel->getShader().getTextureList().getTextureSampler(GMTextureType::Ambient).addFrame(texture);
		createModelDataProxy(d->context, quadModel);
		d->filterQuad = new GMGameObject(quad);
		d->filterQuad->setContext(d->context);
	}
}

void GMGraphicEngine::generateShadowBuffer(const GMGameObjectContainer& forwardRenderingObjects, const GMGameObjectContainer& deferredRenderingObjects)
{
	D(d);
	d->isDrawingShadow = true;

	if (!d->shadowDepthFramebuffers)
	{
		createShadowFramebuffers(&d->shadowDepthFramebuffers);
		resetCSM();
	}
	else
	{
		if (d->shadow.width != d->lastShadow.width || d->shadow.height != d->lastShadow.height)
		{
			d->shadowDepthFramebuffers->destroy();
			createShadowFramebuffers(&d->shadowDepthFramebuffers);
		}

		if (d->shadow.cascades != d->lastShadow.cascades)
		{
			resetCSM();
		}

		if (d->shadow.camera != d->lastShadow.camera)
		{
			ICSMFramebuffers* csm = getCSMFramebuffers(); // csm和d->shadowDepthFramebuffers其实是同一个对象
			for (GMCascadeLevel i = 0; i < d->shadow.cascades; ++i)
			{
				// 创建每一个cascade的viewport
				csm->setEachCascadeEndClip(i);
			}
			resetCSM();
		}
	}

	GM_ASSERT(d->shadowDepthFramebuffers);
	d->shadowDepthFramebuffers->clear(GMFramebuffersClearType::Depth);
	d->shadowDepthFramebuffers->bind();

	// 遍历每个cascaded level
	ICSMFramebuffers* csm = getCSMFramebuffers(); // csm和d->shadowDepthFramebuffers其实是同一个对象
	for (auto i = csm->cascadedBegin(); i != csm->cascadedEnd(); ++i)
	{
		csm->applyCascadedLevel(i);
		draw(forwardRenderingObjects);
		draw(deferredRenderingObjects);
	}

	d->shadowDepthFramebuffers->unbind();
	d->isDrawingShadow = false;
}

bool GMGraphicEngine::needUseFilterFramebuffer()
{
	GMFilterMode::Mode filterMode = getCurrentFilterMode();
	return (filterMode != GMFilterMode::None || needHDR());
}

void GMGraphicEngine::bindFilterFramebuffer()
{
	IFramebuffers* filterFramebuffers = getFilterFramebuffers();
	GM_ASSERT(filterFramebuffers);
	filterFramebuffers->bind();
}

void GMGraphicEngine::clearFilterFramebuffer()
{
	IFramebuffers* filterFramebuffers = getFilterFramebuffers();
	GM_ASSERT(filterFramebuffers);
	filterFramebuffers->clear();
}

void GMGraphicEngine::unbindFilterFramebuffer()
{
	IFramebuffers* filterFramebuffers = getFilterFramebuffers();
	GM_ASSERT(filterFramebuffers);
	filterFramebuffers->unbind();
}

void GMGraphicEngine::drawFilterFramebuffer()
{
	getFilterQuad()->draw();
}

const GMMat4& GMGraphicEngine::getCascadeCameraVPMatrix(GMCascadeLevel level)
{
	D(d);
	return d->shadowCameraVPmatrices[level];
}

GMFramebuffersStack& GMGraphicEngine::getFramebuffersStack()
{
	D(d);
	return d->framebufferStack;
}

void GMGraphicEngine::setShadowSource(const GMShadowSourceDesc& desc)
{
	D(d);
	d->shadow = desc;
	++d->shadow.version;
}

GMCamera& GMGraphicEngine::getCamera()
{
	D(d);
	return d->camera;
}

void GMGraphicEngine::setCamera(const GMCamera& camera)
{
	D(d);
	d->camera = camera;
	d->camera.updateViewMatrix();
}

void GMGraphicEngine::beginBlend(
	GMS_BlendFunc sfactorRGB,
	GMS_BlendFunc dfactorRGB,
	GMS_BlendOp opRGB,
	GMS_BlendFunc sfactorAlpha,
	GMS_BlendFunc dfactorAlpha,
	GMS_BlendOp opAlpha
)
{
	D(d);
	++d->blendState.blendRefCount;
	d->blendState.enabled = true;
	d->blendState.sourceRGB = sfactorRGB;
	d->blendState.destRGB = dfactorRGB;
	d->blendState.opRGB = opRGB;
	d->blendState.sourceAlpha = sfactorAlpha;
	d->blendState.destAlpha = dfactorAlpha;
	d->blendState.opAlpha = opAlpha;
}

void GMGraphicEngine::endBlend()
{
	D(d);
	if (--d->blendState.blendRefCount == 0)
	{
		d->blendState.enabled = false;
	}
}

GMRenderTechniqueManager* GMGraphicEngine::getRenderTechniqueManager()
{
	D(d);
	return d->renderTechniqueManager.get();
}

GMPrimitiveManager* GMGraphicEngine::getPrimitiveManager()
{
	D(d);
	return d->primitiveManager.get();
}

bool GMGraphicEngine::msgProc(const GMMessage& e)
{
	return false;
}

GMConfigs& GMGraphicEngine::getConfigs()
{
	D(d);
	return d->configs;
}

void GMGraphicEngine::createModelDataProxy(const IRenderContext* context, GMModel* model, bool transfer)
{
	if (model)
	{
		GMModelDataProxy* modelDataProxy = model->getModelDataProxy();
		if (!modelDataProxy)
		{
			GM.getFactory()->createModelDataProxy(context, model, &modelDataProxy);
			model->setModelDataProxy(modelDataProxy);
		}
		if (transfer)
			modelDataProxy->transfer();
	}
}

bool GMGraphicEngine::isCurrentMainThread()
{
	D(d);
	return GMThread::getCurrentThreadId() == d->mtid;
}

GMLightIndex GMGraphicEngine::addLight(AUTORELEASE ILight* light)
{
	D(d);
	if (std::find(d->lights.begin(), d->lights.end(), light) == d->lights.end())
	{
		d->lights.push_back(light);
		update(GMUpdateDataType::LightChanged);
	}
	return d->lights.size();
}

ILight* GMGraphicEngine::getLight(GMLightIndex index)
{
	D(d);
	if (index <= 0 || index > d->lights.size())
		return nullptr;
	return d->lights[index - 1];
}

bool GMGraphicEngine::removeLight(ILight* light)
{
	D(d);
	auto iter = std::find(d->lights.begin(), d->lights.end(), light);
	if (iter == d->lights.end())
		return false;

	d->lights.erase(iter);
	return true;
}

bool GMGraphicEngine::removeLight(GMLightIndex index)
{
	D(d);
	if (d->lights.size() >= index)
		return false;

	d->lights.erase(d->lights.begin() + index);
	return true;
}

void GMGraphicEngine::removeLights()
{
	D(d);
	d->deleteLights();
	update(GMUpdateDataType::LightChanged);
}

void GMGraphicEngine::setStencilOptions(const GMStencilOptions& options)
{
	D(d);
	d->stencilOptions = options;
}

const GMStencilOptions& GMGraphicEngine::getStencilOptions()
{
	D(d);
	return d->stencilOptions;
}

void GMGraphicEngine::setShaderLoadCallback(IShaderLoadCallback* cb)
{
	D(d);
	d->shaderLoadCallback = cb;
}

const GMShaderVariablesDesc& GMGraphicEngine::getDefaultShaderVariablesDesc()
{
	return s_defaultShaderVariablesDesc;
}

const GMGlobalBlendStateDesc& GMGraphicEngine::getGlobalBlendState()
{
	D(d);
	return d->blendState;
}

bool GMGraphicEngine::isDrawingShadow()
{
	D(d);
	return d->isDrawingShadow;
}

const GMShadowSourceDesc& GMGraphicEngine::getShadowSourceDesc()
{
	D(d);
	return d->shadow;
}

END_NS
