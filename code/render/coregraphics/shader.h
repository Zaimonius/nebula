#pragma once
//------------------------------------------------------------------------------
/**
	A shader represents an entire shader resource, containing several stages and programs.

	ShaderId - A shader resource handle. It cannot be used for anything other than quering
	the shader file (usually suffixed with .fx) for reflection information.

	ShaderStateId - An instantiation of either all or a subset of the variables in that shader.
	The state contains the, well, state of all resources (constant buffers, textures, read/write buffers)
	as well as the constant values.

	ShaderProgramId - The shader will most likely have a set of shader permutations, and those permutations
	are called programs. The ShaderProgramId binds both the shader, and an associated permutation in one
	call.

	ShaderConstantId - The resources applied with the ShaderStateId are more atomically defined 
	as constants, and serves both as individual shader constants (variables in CPU time, constant in GPU time)
	as well as the resource binds to that state. They are bound to a inherently bound to a state, and such
	modifying a constant requires both the state and constant id for that state. 

	(C)2017-2018 Individual contributors, see AUTHORS file
*/
//------------------------------------------------------------------------------
#include "ids/id.h"
#include "ids/idpool.h"
#include "resources/resourceid.h"
#include "coregraphics/shaderfeature.h"
#include "util/variant.h"

namespace CoreGraphics
{

struct ConstantBufferId;
struct TextureId;
struct ShaderRWTextureId;
struct ShaderRWBufferId;
struct RenderTextureId;
struct ResourceTableId;
struct ResourceTableLayoutId;
struct ResourcePipelineId;

RESOURCE_ID_TYPE(ShaderId);				// 32 bits container, 24 bits resource, 8 bits type
ID_24_8_24_8_NAMED_TYPE(ShaderProgramId, shaderId, shaderType, programId, programType);		// 32 bits shader, 24 bits program, 8 bits type

ID_32_TYPE(DerivativeStateId);			// 32 bits derivative state (already created from an ordinary state)

struct ShaderCreateInfo
{
	const Resources::ResourceName name;
};

enum ShaderConstantType
{
	UnknownVariableType,
	IntVariableType,
	FloatVariableType,
	VectorVariableType,		// float4
	Vector2VariableType,	// float2
	MatrixVariableType,
	BoolVariableType,
	TextureVariableType,
	SamplerVariableType,
	ConstantBufferVariableType,
	ImageReadWriteVariableType,
	BufferReadWriteVariableType
};

enum ShaderPipeline
{
	InvalidPipeline,
	GraphicsPipeline,
	ComputePipeline
};

struct ConstantBinding
{
	uint offset, arraySize, byteSize;
};

/// get constant type as string
Util::String ConstantTypeToString(const ShaderConstantType& type);

/// create new shader
const ShaderId CreateShader(const ShaderCreateInfo& info);
/// destroy shader
void DestroyShader(const ShaderId id);

/// get shader by name
const ShaderId ShaderGet(const Resources::ResourceName& name);

/// create resource table from shader
const ResourceTableId ShaderCreateResourceTable(const ShaderId id, const IndexT group);
/// create constant buffer from shader using name (don't use too frequently)
const ConstantBufferId ShaderCreateConstantBuffer(const ShaderId id, const Util::StringAtom& name);
/// create constant buffer from index
const ConstantBufferId ShaderCreateConstantBuffer(const ShaderId id, const IndexT cbIndex);
/// get constant buffer binding by name
const ConstantBinding ShaderGetConstantBinding(const ShaderId id, const Util::StringAtom& name);
/// get constant buffer binding by index
const ConstantBinding ShaderGetConstantBinding(const ShaderId id, const IndexT cIndex);
/// get name of constant buffer wherein constant with name resides
const Util::StringAtom ShaderGetConstantBlockName(const ShaderId id, const Util::StringAtom& name);
/// get name of constant buffer where in constant with index resides
const Util::StringAtom ShaderGetConstantBlockName(const ShaderId id, const IndexT cIndex);
/// get count of constant buffer bindings (for iteration)
const SizeT ShaderGetConstantBindingsCount(const ShaderId id);


/// get resource table layout
CoreGraphics::ResourceTableLayoutId ShaderGetResourceTableLayout(const ShaderId id, const IndexT group);
/// get pipeline layout for shader
CoreGraphics::ResourcePipelineId ShaderGetResourcePipeline(const ShaderId id);

/// get the number of constants from shader
SizeT ShaderGetConstantCount(const ShaderId id);
/// get type of variable by index
ShaderConstantType ShaderGetConstantType(const ShaderId id, const IndexT i);
/// get type of variable by index
ShaderConstantType ShaderGetConstantType(const ShaderId id, const Util::StringAtom& name);
/// get name of variable by index
Util::StringAtom ShaderGetConstantName(const ShaderId id, const IndexT i);

/// get the number of constant buffers from shader
SizeT ShaderGetConstantBufferCount(const ShaderId id);
/// get size of constant buffer
SizeT ShaderGetConstantBufferSize(const ShaderId id, const IndexT i);
/// get name of constnat buffer
Util::StringAtom ShaderGetConstantBufferName(const ShaderId id, const IndexT i);
/// get slot of shader resource
IndexT ShaderGetResourceSlot(const ShaderId id, const Util::StringAtom& name);

/// get programs
const Util::Dictionary<ShaderFeature::Mask, ShaderProgramId>& ShaderGetPrograms(const ShaderId id);
/// get name of program
Util::StringAtom ShaderProgramGetName(const ShaderProgramId id);

/// convert shader feature from string to mask
ShaderFeature::Mask ShaderFeatureFromString(const Util::String& str);

/// get shader program id from masks, this allows us to apply a shader program directly in the future
const CoreGraphics::ShaderProgramId ShaderGetProgram(const ShaderId id, const CoreGraphics::ShaderFeature::Mask program);

class ShaderPool;
extern ShaderPool* shaderPool;

} // namespace CoreGraphics
