﻿#ifndef __CORE_RENDER_TREE_H__
#define __CORE_RENDER_TREE_H__
#include <gmecommon.h>
#include <QtCore>
#include <gamemachine.h>
#include "scene_model.h"

namespace core
{
	struct Asset
	{
		GMSceneAsset asset;
		GMPhysicsShapeAsset shape;
		GMGameObject* object = nullptr;
	};

	typedef QList<RenderTree*> RenderTrees;
	typedef QList<RenderNode*> SelectedNodes;

	inline bool operator ==(const Asset& lhs, const Asset& rhs)
	{
		return (lhs.asset == rhs.asset) && (lhs.shape == rhs.shape) && (lhs.object == rhs.object);
	}

	struct RenderMouseDetails
	{
		bool mouseDown;
		int position[2];
		int lastPosition[2];
	};

	struct RenderContext
	{
		Handler* handler;
		SceneControl* control;
		RenderTree* tree;
	};

	enum EventResult
	{
		ER_OK,
		ER_Continue,
	};

	class RenderNode
	{
	public:
		Asset asset();
		void setAsset(const Asset& asset);

	public:
		virtual void render(const RenderContext&);
		virtual bool hitTest(const RenderContext&);
		virtual EventResult onMousePress(const RenderContext& ctx, const RenderMouseDetails& details);
		virtual EventResult onMouseRelease(const RenderContext& ctx, const RenderMouseDetails& details);
		virtual EventResult onMouseMove(const RenderContext& ctx, const RenderMouseDetails& details);

	protected:
		virtual bool isAssetReady() const;
		virtual void renderAsset(const RenderContext&);
		virtual void initAsset(const RenderContext& ctx);

	protected:
		Asset m_asset;
	};

	class RenderTree : public QObject
	{
		Q_OBJECT

	public:
		RenderTree(SceneControl*);
		~RenderTree();

	public:
		void appendNode(RenderNode* node);
		void reset();

	public:
		virtual void render(bool cleanBuffer);
		virtual RenderNode* hitTest(int x, int y);
		virtual void onRenderTreeSet();
		virtual void onMousePress(const RenderMouseDetails& details, RenderNode* hitTestResult);
		virtual void onMouseRelease(const RenderMouseDetails& details);
		virtual void onMouseMove(const RenderMouseDetails& details);
		virtual bool onPropertyChanged(ChangedProperty);

	public:
		virtual void clearSelect();
		virtual void select(RenderNode*);
		virtual SelectedNodes selectedNotes();

	protected:
		virtual RenderNode* find(GMGameObject*);

	protected:
		SceneControl* control();
		RenderContext& context();
		QList<RenderNode*>& nodes();

	private:
		QList<RenderNode*> m_nodes;
		SelectedNodes m_selection;
		RenderTree* m_childTree = nullptr;
		SceneControl* m_control = nullptr;
		RenderContext m_ctx;
	};
}

#endif