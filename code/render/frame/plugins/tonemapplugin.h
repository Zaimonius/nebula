#pragma once
//------------------------------------------------------------------------------
/**
	Implements tonemapping as a script algorithm
	
	(C) 2016-2020 Individual contributors, see AUTHORS file
*/
//------------------------------------------------------------------------------
#include "frameplugin.h"
#include "coregraphics/shader.h"
#include "coregraphics/resourcetable.h"
#include "renderutil/drawfullscreenquad.h"
namespace Frame
{
class TonemapPlugin : public FramePlugin
{
public:
	/// constructor
	TonemapPlugin();
	/// destructor
	virtual ~TonemapPlugin();

	/// setup algorithm
	void Setup() override;
	/// discard algorithm
	void Discard() override;
    /// resize
    void Resize() override;

private:

	CoreGraphics::TextureId downsample2x2;
	CoreGraphics::TextureId copy;

	CoreGraphics::ShaderId shader;
	CoreGraphics::ResourceTableId tonemapTable;
	IndexT constantsSlot, colorSlot, prevSlot;

	CoreGraphics::ShaderProgramId program;

	CoreGraphics::ConstantBinding timevar;
	CoreGraphics::ConstantBufferId constants;
	RenderUtil::DrawFullScreenQuad fsq;
};

} // namespace Frame
