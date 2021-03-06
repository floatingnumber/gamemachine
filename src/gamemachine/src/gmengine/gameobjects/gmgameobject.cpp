﻿#include "stdafx.h"
#include "gmgameobject.h"
#include "gmengine/gmgameworld.h"
#include "gmdata/glyph/gmglyphmanager.h"
#include "foundation/gamemachine.h"
#include "gmassets.h"
#include "gmanimationobjecthelper.h"
#include "../gmcomputeshadermanager.h"
#include <gmphysicsworld.h>
#include "gmgameobject_p.h"

BEGIN_NS

namespace
{
	static GMString s_defaultComputeShaderCode;

	void calculateAABB(bool indexMode, GMPart* part, REF GMVec3& min, REF GMVec3& max)
	{
		if (!indexMode)
		{
			for (const auto& v : part->vertices())
			{
				GMVec3 cmp(v.positions[0], v.positions[1], v.positions[2]);
				min = MinComponent(cmp, min);
				max = MaxComponent(cmp, max);
			}
		}
		else
		{
			const GMVertices& v = part->vertices();
			for (const auto& i : part->indices())
			{
				GMVec3 cmp(v[i].positions[0], v[i].positions[1], v[i].positions[2]);
				min = MinComponent(cmp, min);
				max = MaxComponent(cmp, max);
			}
		}
	}

	bool isInsideCameraFrustum(GMCamera* camera, GMVec3 (&points)[8])
	{
		GM_ASSERT(camera);
		const GMFrustum& frustum = camera->getFrustum();
		// 如果min和max出现在了某个平面的两侧，
		GMFrustumPlanes planes;
		frustum.getPlanes(planes);
		return GMCamera::isBoundingBoxInside(planes, points);
	}
}

void GMGameObjectPrivate::setAutoUpdateTransformMatrix(bool autoUpdateTransformMatrix) GM_NOEXCEPT
{
	autoUpdateTransformMatrix = autoUpdateTransformMatrix;
}

void GMGameObjectPrivate::releaseAllBufferHandle()
{
	auto& instance = GMComputeShaderManager::instance();
	instance.releaseHandle(cullAABBsBuffer);
	cullAABBsBuffer = 0;

	instance.releaseHandle(cullGPUResultBuffer);
	cullGPUResultBuffer = 0;

	instance.releaseHandle(cullCPUResultBuffer);
	cullCPUResultBuffer = 0;

	instance.releaseHandle(cullFrustumBuffer);
	cullFrustumBuffer = 0;

	instance.releaseHandle(cullAABBsSRV);
	cullAABBsSRV = 0;

	instance.releaseHandle(cullResultUAV);
	cullResultUAV = 0;
}

void GMGameObjectPrivate::updateTransformMatrix()
{
	transforms.transformMatrix = transforms.scaling * QuatToMatrix(transforms.rotation) * transforms.translation;
}

GM_DEFINE_PROPERTY(GMGameObject, GMGameObjectRenderPriority, RenderPriority, renderPriority)

GMGameObject::GMGameObject()
{
	GM_CREATE_DATA();

	D(d);
	d->helper = new GMAnimationGameObjectHelper(this);
}

GMGameObject::GMGameObject(GMAsset asset)
	: GMGameObject()
{
	D(d);
	setAsset(asset);
	d->updateTransformMatrix();
}

GMGameObject::~GMGameObject()
{
	if (GM_HAS_DATA())
	{
		D(d);
		d->releaseAllBufferHandle();
		GM_delete(d->helper);
	}
}

void GMGameObject::setAsset(GMAsset asset)
{
	D(d);
	if (!asset.isScene())
	{
		if (!asset.isModel())
		{
			GM_ASSERT(false);
			gm_error(gm_dbg_wrap("Asset must be a 'scene' or a 'model' type."));
			return;
		}
		else
		{
			asset = GMScene::createSceneFromSingleModel(asset);
		}
	}
	d->asset = asset;
}


GMAsset GMGameObject::getAsset()
{
	D(d);
	return d->asset;
}

GMScene* GMGameObject::getScene()
{
	D(d);
	return d->asset.getScene();
}

const GMScene* GMGameObject::getScene() const
{
	D(d);
	return d->asset.getScene();
}

GMModel* GMGameObject::getModel()
{
	GMScene* scene = getScene();
	if (!scene)
		return nullptr;

	if (scene->getModels().size() > 1)
	{
		gm_warning(gm_dbg_wrap("Models are more than one. So it will return nothing."));
		return nullptr;
	}

	return scene->getModels()[0].getModel();
}

void GMGameObject::setWorld(GMGameWorld* world)
{
	D(d);
	GM_ASSERT(!d->world);
	d->world = world;
}

GMGameWorld* GMGameObject::getWorld()
{
	D(d);
	return d->world;
}

void GMGameObject::setPhysicsObject(AUTORELEASE GMPhysicsObject* phyObj)
{
	D(d);
	d->physics.reset(phyObj);
	d->physics->setGameObject(this);
}

void GMGameObject::foreachModel(std::function<void(GMModel*)> cb)
{
	GMScene* scene = getScene();
	if (scene)
	{
		for (auto& model : scene->getModels())
		{
			cb(model.getModel());
		}
	}
}

void GMGameObject::setCullComputeShaderProgram(IComputeShaderProgram* shaderProgram)
{
	D(d);
	d->cullShaderProgram = shaderProgram;
}

void GMGameObject::onAppendingObjectToWorld()
{
	D(d);
	if (d->cullOption == GMGameObjectCullOption::AABB)
		makeAABB();
}

void GMGameObject::draw()
{
	cull();
	foreachModel([this](GMModel* m) {
		drawModel(getContext(), m);
	});
	endDraw();
}


void GMGameObject::update(GMDuration dt)
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		return d->helper->update(dt);
	}
}

bool GMGameObject::canDeferredRendering()
{
	D(d);
	GMScene* scene = getScene();
	if (scene)
	{
		for (decltype(auto) model : scene->getModels())
		{
			if (model.getModel()->getShader().getBlend() == true)
				return false;

			if (model.getModel()->getShader().getVertexColorOp() == GMS_VertexColorOp::Replace)
				return false;

			if (model.getModel()->getType() == GMModelType::Custom)
				return false;
		}
	}
	return true;
}

const IRenderContext* GMGameObject::getContext()
{
	D(d);
	return d->context;
}

void GMGameObject::play()
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		d->helper->play();
	}
}

void GMGameObject::pause()
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		d->helper->pause();
	}
}

bool GMGameObject::isPlaying()
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		return d->helper->isPlaying();
	}
	return false;
}

void GMGameObject::reset(bool update)
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		d->helper->reset(update);
	}
}

GMsize_t GMGameObject::getAnimationCount()
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		return d->helper->getAnimationCount();
	}
	return 0;
}

void GMGameObject::setAnimation(GMsize_t index)
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		d->helper->setAnimation(index);
	}
}

Vector<GMString> GMGameObject::getAnimationNames()
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		return d->helper->getAnimationNames();
	}
	return Vector<GMString>();
}

GMsize_t GMGameObject::getAnimationIndexByName(const GMString& name)
{
	D(d);
	if (getAnimationType() != GMAnimationType::NoAnimation)
	{
		GM_ASSERT(d->helper);
		return d->helper->getAnimationIndexByName(name);
	}
	return -1;
}

void GMGameObject::setScaling(const GMMat4& scaling)
{
	D(d);
	d->transforms.scaling = scaling;
	if (d->autoUpdateTransformMatrix)
		d->updateTransformMatrix();
}

void GMGameObject::setTranslation(const GMMat4& translation)
{
	D(d);
	d->transforms.translation = translation;
	if (d->autoUpdateTransformMatrix)
		d->updateTransformMatrix();
}

void GMGameObject::setRotation(const GMQuat& rotation)
{
	D(d);
	d->transforms.rotation = rotation;
	if (d->autoUpdateTransformMatrix)
		d->updateTransformMatrix();
}

void GMGameObject::beginUpdateTransform()
{
	D(d);
	d->setAutoUpdateTransformMatrix(false);
}

void GMGameObject::endUpdateTransform()
{
	D(d);
	d->setAutoUpdateTransformMatrix(true);
	d->updateTransformMatrix();
}


void GMGameObject::setCullOption(GMGameObjectCullOption option, GMCamera* camera /*= nullptr*/)
{
	D(d);
	d->cullOption = option;
	// setCullOption一定要在将顶点数据传输到显存之前设置，否则顶点信息将不会在内处中保留，从而无法计算AABB

	d->cullCamera = camera;

	// 如果不需要事先裁剪，重置Shader状态
	if (option == GMGameObjectCullOption::NoCull)
	{
		Vector<GMAsset>& models = getScene()->getModels();
		for (GMsize_t i = 0; i < d->cullAABB.size(); ++i)
		{
			auto& shader = models[i].getModel()->getShader();
			shader.setCulled(false);
		}
	}
}


GMAnimationType GMGameObject::getAnimationType() const
{
	const GMScene* scene = getScene();
	if (scene)
	{
		return scene->getAnimationType();
	}
	return GMAnimationType::NoAnimation;
}

bool GMGameObject::getVisible() const GM_NOEXCEPT
{
	D(d);
	return d->attributes.visible;
}

const GMMat4& GMGameObject::getTransform() const GM_NOEXCEPT
{
	D(d);
	return d->transforms.transformMatrix;
}

const GMMat4& GMGameObject::getScaling() const GM_NOEXCEPT {
	D(d);
	return d->transforms.scaling;
}

const GMMat4& GMGameObject::getTranslation() const GM_NOEXCEPT
{
	D(d);
	return d->transforms.translation;
}

const GMQuat& GMGameObject::getRotation() const GM_NOEXCEPT {
	D(d);
	return d->transforms.rotation;
}

GMPhysicsObject* GMGameObject::getPhysicsObject()
{
	D(d);
	return d->physics.get();
}

void GMGameObject::setContext(const IRenderContext* context)
{
	D(d);
	d->context = context;
}

void GMGameObject::setVisible(bool visible) const GM_NOEXCEPT
{
	D(d);
	d->attributes.visible = visible;
}

void GMGameObject::setDefaultCullShaderCode(const GMString& code)
{
	s_defaultComputeShaderCode = code;
}

void GMGameObject::drawModel(const IRenderContext* context, GMModel* model)
{
	D(d);
	if (!d->attributes.visible)
		return;

	if (!model->getShader().getVisible() || model->getShader().isCulled())
		return;

	IGraphicEngine* engine = context->getEngine();
	ITechnique* technique = engine->getTechnique(model->getType());
	if (technique != d->drawContext.currentTechnique)
	{
		if (d->drawContext.currentTechnique)
			d->drawContext.currentTechnique->endScene();

		technique->beginScene(getScene());
		d->drawContext.currentTechnique = technique;
	}

	technique->beginModel(model, this);
	technique->draw(model);
	technique->endModel();
}

void GMGameObject::endDraw()
{
	D(d);
	if (d->drawContext.currentTechnique)
		d->drawContext.currentTechnique->endScene();

	d->drawContext.currentTechnique = nullptr;
}

void GMGameObject::makeAABB()
{
	D(d);
	typedef std::remove_reference_t<decltype(d->cullAABB[0])> AABB;
	const Vector<GMAsset>& models = getScene()->getModels();
	for (auto modelAsset : models)
	{
		GMModel* model = modelAsset.getModel();
		GM_ASSERT(model->isNeedTransfer());
		const GMParts& parts = model->getParts();
		GMVec3 min(FLT_MAX, FLT_MAX, FLT_MAX);
		GMVec3 max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (auto part : parts)
		{
			calculateAABB(model->getDrawMode() == GMModelDrawMode::Index, part, min, max);
		}

		// 将AABB和gameobject添加到成员数据中
		AABB aabb = {
			GMVec4(min.getX(), min.getY(), min.getZ(), 1),
			GMVec4(min.getX(), min.getY(), max.getZ(), 1),
			GMVec4(min.getX(), max.getY(), max.getZ(), 1),
			GMVec4(max.getX(), max.getY(), max.getZ(), 1),
			GMVec4(min.getX(), max.getY(), min.getZ(), 1),
			GMVec4(max.getX(), min.getY(), max.getZ(), 1),
			GMVec4(max.getX(), max.getY(), min.getZ(), 1),
			GMVec4(max.getX(), min.getY(), min.getZ(), 1),
		};
		d->cullAABB.push_back(aabb);
	}
}

IComputeShaderProgram* GMGameObject::getCullShaderProgram()
{
	D(d);
	if (!d->context)
		return nullptr;

	if (d->cullShaderProgram)
		return d->cullShaderProgram;

	if (s_defaultComputeShaderCode.isEmpty())
		return nullptr;

	auto defaultProgram = GMComputeShaderManager::instance().getComputeShaderProgram(d->context, GMCS_GAMEOBJECT_CULL, L".", s_defaultComputeShaderCode, L"main");
	if (defaultProgram)
		return defaultProgram;

	return defaultProgram;
}

void GMGameObject::cull()
{
	D(d);
	if (d->cullOption == GMGameObjectCullOption::AABB)
	{
		struct CullResult
		{
			GMint32 visible = 0;
		};

		bool sizeChanged = d->cullAABB.size() != d->cullSize;
		d->cullSize = d->cullAABB.size();

		IComputeShaderProgram* cullShaderProgram = getCullShaderProgram();
		if (cullShaderProgram && d->cullGPUAccelerationValid)
		{
			if (sizeChanged)
			{
				d->releaseAllBufferHandle();

				typedef std::remove_reference_t<decltype(d->cullAABB[0])> AABB;
				if (cullShaderProgram->createBuffer(sizeof(AABB), gm_sizet_to_uint(d->cullSize), d->cullAABB.data(), GMComputeBufferType::Structured, &d->cullAABBsBuffer) &&
					cullShaderProgram->createBuffer(sizeof(CullResult), gm_sizet_to_uint(d->cullSize), nullptr, GMComputeBufferType::UnorderedStructured, &d->cullGPUResultBuffer) &&
					cullShaderProgram->createBuffer(sizeof(GMFrustumPlanes), 1u, NULL, GMComputeBufferType::Constant, &d->cullFrustumBuffer) &&
					cullShaderProgram->createBufferShaderResourceView(d->cullAABBsBuffer, &d->cullAABBsSRV) &&
					cullShaderProgram->createReadOnlyBufferFrom(d->cullGPUResultBuffer, &d->cullCPUResultBuffer) &&
					cullShaderProgram->createBufferUnorderedAccessView(d->cullGPUResultBuffer, &d->cullResultUAV))
				{
					// create succeed
				}
				else
				{
					gm_warning(gm_dbg_wrap("GMGameObject create buffer or resource view failed. Cull Compute shader has been shut down for current game object."));
					d->cullGPUAccelerationValid = false;
					return;
				}
			}

			GMCamera* camera = d->cullCamera ? d->cullCamera : &getContext()->getEngine()->getCamera();
			GMFrustumPlanes planes;
			camera->getFrustum().getPlanes(planes);
			cullShaderProgram->setBuffer(d->cullFrustumBuffer, GMComputeBufferType::Constant, &planes, sizeof(GMFrustumPlanes));
			cullShaderProgram->bindConstantBuffer(d->cullFrustumBuffer);
			GMComputeSRVHandle srvs[] = { d->cullAABBsSRV };
			cullShaderProgram->bindShaderResourceView(1, srvs);
			GMComputeUAVHandle uavs[] = { d->cullResultUAV };
			cullShaderProgram->bindUnorderedAccessView(1, uavs);
			cullShaderProgram->dispatch(gm_sizet_to_uint(d->cullAABB.size()), 1, 1);

			bool canReadFromGPU = cullShaderProgram->canRead(d->cullGPUResultBuffer);
			GMComputeBufferHandle resultHandle = canReadFromGPU ? d->cullGPUResultBuffer : d->cullCPUResultBuffer;
			if (!canReadFromGPU)
				cullShaderProgram->copyBuffer(resultHandle, d->cullGPUResultBuffer);
			CullResult* resultPtr = static_cast<CullResult*>(cullShaderProgram->mapBuffer(resultHandle));
			Vector<GMAsset>& models = getScene()->getModels();
			for (GMsize_t i = 0; i < models.size(); ++i)
			{
				auto& shader = models[i].getModel()->getShader();
				shader.setCulled(resultPtr[i].visible == 0);
			}
			cullShaderProgram->unmapBuffer(d->cullCPUResultBuffer);
		}
		else
		{
			Vector<GMAsset>& models = getScene()->getModels();
			// 计算每个Model的AABB是否与相机Frustum有交集，如果没有，则不进行绘制
			GM_ASSERT(d->cullAABB.size() == models.size());

			GMAsync::blockedAsync(
				GMAsync::Async,
				GM.getRunningStates().systemInfo.numberOfProcessors,
				d->cullAABB.begin(),
				d->cullAABB.end(),
				[d, &models, this](auto begin, auto end) {
				// 计算一下数据偏移
				for (auto iter = begin; iter != end; ++iter)
				{
					GMVec3 vertices[8];
					GMsize_t offset = iter - d->cullAABB.begin();
					auto& shader = models[offset].getModel()->getShader();

					for (auto i = 0; i < 8; ++i)
					{
						vertices[i] = d->cullAABB[offset].points[i] * d->transforms.transformMatrix;
					}

					if (isInsideCameraFrustum(d->cullCamera ? d->cullCamera : &getContext()->getEngine()->getCamera(), vertices))
						shader.setCulled(false);
					else
						shader.setCulled(true);
				}
			}
			);
		}
	}
}

GM_PRIVATE_OBJECT_ALIGNED(GMCubeMapGameObject)
{
	GM_DECLARE_PUBLIC(GMCubeMapGameObject)
	GMVec3 min;
	GMVec3 max;
	GMShader shader;
	void createCubeMap(GMTextureAsset texture);
};

void GMCubeMapGameObjectPrivate::createCubeMap(GMTextureAsset texture)
{
	GMfloat vertices[] = {
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,

		1.0f, -1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,

		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,

		1.0f,  1.0f,  1.0f,
		1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,
		1.0f,  1.0f,  1.0f,

		1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		1.0f, -1.0f, -1.0f,

		1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		1.0f, -1.0f,  1.0f
	};

	GMfloat v[] = {
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
	};

	GMModel* model = new GMModel();
	model->setType(GMModelType::CubeMap);
	model->getShader().getTextureList().getTextureSampler(GMTextureType::CubeMap).addFrame(texture);
	GMPart* part = new GMPart(model);
	for (GMuint32 i = 0; i < 12; i++)
	{
		GMVertex V0 = { { vertices[i * 9 + 0], vertices[i * 9 + 1], vertices[i * 9 + 2] },{ vertices[i * 9 + 0], vertices[i * 9 + 1], vertices[i * 9 + 2] } };
		GMVertex V1 = { { vertices[i * 9 + 3], vertices[i * 9 + 4], vertices[i * 9 + 5] },{ vertices[i * 9 + 3], vertices[i * 9 + 4], vertices[i * 9 + 5] } };
		GMVertex V2 = { { vertices[i * 9 + 6], vertices[i * 9 + 7], vertices[i * 9 + 8] },{ vertices[i * 9 + 6], vertices[i * 9 + 7], vertices[i * 9 + 8] } };
		part->vertex(V0);
		part->vertex(V1);
		part->vertex(V2);
	}

	P_D(pd);
	pd->setAsset(GMScene::createSceneFromSingleModel(GMAsset(GMAssetType::Model, model)));
}

GMCubeMapGameObject::GMCubeMapGameObject(GMTextureAsset texture)
{
	GM_CREATE_DATA();
	GM_SET_PD();
	D(d);
	d->createCubeMap(texture);
}

GMCubeMapGameObject::~GMCubeMapGameObject()
{

}

void GMCubeMapGameObject::deactivate()
{
	D(d);
	GMGameWorld* world = getWorld();
	if (world)
		world->getContext()->getEngine()->update(GMUpdateDataType::TurnOffCubeMap);
}

bool GMCubeMapGameObject::canDeferredRendering()
{
	return false;
}

END_NS