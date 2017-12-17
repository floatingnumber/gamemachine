﻿#ifndef __INTERFACES_H__
#define __INTERFACES_H__
#include <glm/fwd.hpp>
#include <input.h>
#include <gmenums.h>

BEGIN_NS

// 前置声明
class GameMachine;
class GMGameWorld;
class GMCamera;
class GMGameObject;
class DrawingList;
class GMGamePackage;
class GMImage;
class GMModel;
class GMGamePackage;
class GMGlyphManager;
class GMUIWindow;
class GMShader;
class GMModelPainter;
class GMMesh;
class GMTextureFrames;
class GMTexture;
class GMAssets;
struct ISoundPlayer;
struct IGamePackageHandler;
struct GraphicSettings;
struct GMCameraLookAt;
struct IDebugOutput;
struct IAudioPlayer;

enum class GameMachineEvent
{
	FrameStart,
	FrameEnd,
	Simulate,
	Render,
	Activate,
	Deactivate,
	Terminate,
};

enum class GameMachineMessageType
{
	None,
	Quit,
	CrashDown,
	Console,
	WindowSizeChanged,
};

struct GameMachineMessage
{
	GameMachineMessage(GameMachineMessageType t = GameMachineMessageType::None, GMint tp = 0, GMint v = 0)
		: msgType(t)
		, type(tp)
		, value(v)
	{}

	GameMachineMessageType msgType = GameMachineMessageType::None;
	GMint type = 0;
	GMint value = 0;
};

GM_INTERFACE(IGameHandler)
{
	virtual void init() = 0;
	virtual void start() = 0;
	virtual void event(GameMachineEvent evt) = 0;
};

GM_INTERFACE(ITexture)
{
	virtual void drawTexture(GMTextureFrames* frames) = 0;
};

enum class GMBufferMode
{
	Normal,
	NoFramebuffer,
};

enum class GMUpdateDataType
{
	ProjectionMatrix,
	ViewMatrix,
};

class GMLight;

//! 图形绘制引擎接口
/*!
  提供最基本的绘制功能。
*/
GM_INTERFACE(IGraphicEngine)
{
	//! 初始化绘制引擎。
	/*!
	  该方法将在GameMachine初始化时被调用。
	*/
	virtual void init() = 0;

	//! 刷新帧缓存，新建一帧。
	/*!
	  此方法保证默认帧缓存将会被清空，但是并不保证其他帧缓存（如G缓存）被清空。
	  如果要清空其它帧缓存，应该调用需要清除的帧缓存的响应方法。
	  此方法将会清除默认帧缓存中的颜色缓存、深度缓存和模板缓存。
	*/
	virtual void newFrame() = 0;

	//! 处理GameMachine消息。
	/*!
	  当GameMachine在处理完系统消息之后，此方法将会被调用。
	  通常用此方法处理一些特殊事件，如窗口大小变化时，需要重新分配帧缓存、G缓存等。
	  \param e GameMachine消息
	  \return 如果此事件被处理，返回true，否则返回false。
	  \sa GameMachineMessage
	*/
	virtual bool event(const GameMachineMessage& e) = 0;

	//! 绘制若干个GMGameObject对象
	/*!
	  绘制GMGameObject对象。这个方法会将绘制好的图元到目标缓存，目标缓存取决于GMBufferMode的值。
	  \param objects 待绘制的对象。
	  \param count 待绘制对象的个数。
	  \param bufferMode 绘制模式。如果模式为GMBufferMode::Normal，程序将按照正常流程绘制，如果模式为GMBufferMode::NoFramebuffer，
	         程序会将绘制结果直接保存在默认帧缓存上，而不会保存在其他帧缓存中。
	*/
	virtual void drawObjects(GMGameObject *objects[], GMuint count, GMBufferMode bufferMode = GMBufferMode::Normal) = 0;

	//! 更新绘制数据。
	/*!
	  调用此方法会将数据更新到绘制的着色器程序中。
	  \param type 需要更新的数据类型。
	*/
	virtual void update(GMUpdateDataType type) = 0;

	//! 增加一个光源。
	/*!
	  将一个光源添加到全局绘制引擎中。
	  光源的表现行为与着色器程序有关，有些图元可能不会使用到光源，有些图元则可能会。
	  \param light 需要添加的光源。
	*/
	virtual void addLight(const GMLight& light) = 0;

	//! 移除所有光源。
	/*!
	  移除引擎中的所有光源。
	*/
	virtual void removeLights() = 0;

	//! 清除当前激活帧缓存下的模板缓存。
	/*!
	  如果当前激活缓存是默认帧缓存，则清除默认帧缓存中的模板缓存。
	  如果当前激活的是其它帧缓存（如G缓存等），则清除它的模板缓存。
	*/
	virtual void clearStencil() = 0;

	//! 开始创建模板缓存。
	/*!
	  在当前激活帧缓存下创建模板缓存。
	  在此方法被执行后，所有的绘制将会被写入进被激活的帧缓存的模板缓存中。
	  \sa endCreateStencil()
	*/
	virtual void beginCreateStencil() = 0;

	//! 结束创建模板缓存。
	/*!
	  结束模板缓存的创建。
	  在此方法被执行后，所有的绘制不会被写入到当前激活的帧缓存的模板缓存中。
	  \sa beginCreateStencil()
	*/
	virtual void endCreateStencil() = 0;

	//* 开始使用帧缓存。
	/*!
	  在此方法被执行后，绘制图元将会根据当前激活的帧缓存的模板缓存进行绘制。
	  \param inverse 表示图元是绘制在模板中，还是模板外。
	  \sa endUseStencil()
	*/
	virtual void beginUseStencil(bool inverse) = 0;

	//! 结束使用帧缓存。
	/*!
	  在此方法被执行后，图元的绘制将不会依据任何模板缓存。
	  \sa beginUseStencil()
	*/
	virtual void endUseStencil() = 0;

	//! 开始进行融合绘制
	/*!
	  决定下一次调用drawObjects时的混合模式。如果在一个绘制流程中多次调用drawObjects，则应该使用此方法，将本帧的画面和当前帧缓存进行
	  融合，否则本帧将会覆盖当前帧缓存已有的所有值。
	  \sa drawObjects(), endBlend()
	*/
	virtual void beginBlend(GMS_BlendFunc sfactor = GMS_BlendFunc::ONE, GMS_BlendFunc dfactor = GMS_BlendFunc::ONE) = 0;

	//! 结束融合绘制
	/*!
	  结束与当前帧缓存的融合。在这种情况下，执行多次drawObjects，会将其输出的帧缓存覆盖多次。
	  \sa drawObjects(), beginBlend()
	*/
	virtual void endBlend() = 0;
};

class GMComponent;
GM_INTERFACE(IRenderer)
{
	virtual void beginModel(GMModel* model, const GMGameObject* parent) = 0;
	virtual void endModel() = 0;
	virtual void beginComponent(GMComponent* component) = 0;
	virtual void endComponent() = 0;
};

GM_INTERFACE(IShaderProgram)
{
	virtual void useProgram() = 0;
	virtual void setMatrix4(const char* name, const GMfloat value[16]) = 0;
	virtual void setVec4(const char* name, const GMfloat value[4]) = 0;
	virtual void setVec3(const char* name, const GMfloat value[3]) = 0;
	virtual void setInt(const char* name, GMint value) = 0;
	virtual void setFloat(const char* name, GMfloat value) = 0;
	virtual void setBool(const char* name, bool value) = 0;
};

GM_INTERFACE(IFactory)
{
	virtual void createGraphicEngine(OUT IGraphicEngine**) = 0;
	virtual void createTexture(GMImage*, OUT ITexture**) = 0;
	virtual void createPainter(IGraphicEngine*, GMModel*, OUT GMModelPainter**) = 0;
	virtual void createGlyphManager(OUT GMGlyphManager**) = 0;
};

#if _WINDOWS
typedef HINSTANCE GMInstance;
typedef HWND GMWindowHandle;
struct GMWindowAttributes
{
	HWND hwndParent;
	LPCTSTR pstrName;
	DWORD dwStyle;
	DWORD dwExStyle;
	RECT rc;
	HMENU hMenu;
	GMInstance instance;
};
#else
struct GMWindowAttributes
{
};
typedef GMuint GMUIInstance;
typedef GMuint GMWindowHandle;
#endif

GM_INTERFACE(IWindow)
{
	virtual IInput* getInputMananger() = 0;
	virtual void update() = 0;
	virtual gm::GMWindowHandle create(const GMWindowAttributes& attrs) = 0;
	virtual void centerWindow() = 0;
	virtual void showWindow() = 0;
	virtual bool handleMessage() = 0;
	virtual GMRect getWindowRect() = 0;
	virtual GMRect getClientRect() = 0;
	virtual GMWindowHandle getWindowHandle() const = 0;
	virtual bool event(const GameMachineMessage& msg) = 0;
	virtual bool isWindowActivate() = 0;
	virtual void setLockWindow(bool lock) = 0;
};

GM_ALIGNED_STRUCT(GMConsoleHandle)
{
	IWindow* window = nullptr;
	IDebugOutput* dbgoutput;
};

// Audio
#ifndef _WAVEFORMATEX_
#define _WAVEFORMATEX_
typedef struct tWAVEFORMATEX
{
	WORD    wFormatTag;
	WORD    nChannels;
	DWORD   nSamplesPerSec;
	DWORD   nAvgBytesPerSec;
	WORD    nBlockAlign;
	WORD    wBitsPerSample;
	WORD    cbSize;
} WAVEFORMATEX;
#endif /* _WAVEFORMATEX_ */

struct GMAudioFileInfo
{
	GMint format;
	const void* data;
	GMint size;
	GMint frequency;
	WAVEFORMATEX waveFormatExHeader;
};

GM_INTERFACE(IAudioStream)
{
	virtual GMuint getBufferSize() = 0; // 每个部分的buffer大小
	virtual GMuint getBufferNum() = 0; // buffer一共分为多少部分
	virtual bool readBuffer(GMbyte* data) = 0;
	virtual void nextChunk(gm::GMlong chunkNum) = 0;
	virtual void rewind() = 0;
};

GM_INTERFACE(IAudioFile)
{
	virtual bool isStream() = 0;
	virtual IAudioStream* getStream() = 0;
	virtual const GMAudioFileInfo& getFileInfo() = 0;
	virtual GMuint getBufferId() = 0;
};

GM_INTERFACE(IAudioSource)
{
	virtual void play(bool loop) = 0;
	virtual void stop() = 0;
	virtual void pause() = 0;
	virtual void rewind() = 0;
};

GM_INTERFACE(IAudioPlayer)
{
	virtual void createPlayerSource(IAudioFile*, OUT gm::IAudioSource** handle) = 0;
};

GM_INTERFACE(IAudioReader)
{
	virtual bool load(GMBuffer& buffer, OUT gm::IAudioFile** f) = 0;
};

END_NS
#endif
