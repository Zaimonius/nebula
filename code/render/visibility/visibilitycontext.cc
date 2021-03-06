//------------------------------------------------------------------------------
//  visobservercontext.cc
//  (C) 2018-2020 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "render/stdneb.h"
#include "visibilitycontext.h"
#include "graphics/graphicsserver.h"

#include "graphics/cameracontext.h"
#include "lighting/lightcontext.h"
#include "lighting/lightprobecontext.h"
#include "models/modelcontext.h"
#include "particles/particlecontext.h"

#include "systems/boxsystem.h"
#include "systems/octreesystem.h"
#include "systems/portalsystem.h"
#include "systems/quadtreesystem.h"
#include "systems/bruteforcesystem.h"

#include "system/cpu.h"
#include "profiling/profiling.h"

#ifndef PUBLIC_BUILD
#include "imgui.h"
#endif

namespace Visibility
{

ObserverContext::ObserverAllocator ObserverContext::observerAllocator;
ObservableContext::ObservableAllocator ObservableContext::observableAllocator;
ObservableContext::ObservableAtomAllocator ObservableContext::observableAtomAllocator;

Util::Array<VisibilitySystem*> ObserverContext::systems;

Jobs::JobPortId ObserverContext::jobPort;
Jobs::JobSyncId ObserverContext::jobInternalSync;
Jobs::JobSyncId ObserverContext::jobInternalSync2;
Jobs::JobSyncId ObserverContext::jobHostSync;
Util::Queue<Jobs::JobId> ObserverContext::runningJobs;

extern void VisibilitySortJob(const Jobs::JobFuncContext& ctx);
extern void VisibilityDependencyJob(const Jobs::JobFuncContext& ctx);

_ImplementContext(ObserverContext, ObserverContext::observerAllocator);

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::Setup(const Graphics::GraphicsEntityId id, VisibilityEntityType entityType)
{
	const Graphics::ContextEntityId cid = GetContextId(id);
	observerAllocator.Get<Observer_EntityType>(cid.id) = entityType;
	observerAllocator.Get<Observer_EntityId>(cid.id) = id;

	// go through observerable objects and allocate a slot for the object, and set it to the default visible state
	const Util::Array<Graphics::GraphicsEntityId>& ids = ObservableContext::observableAllocator.GetArray<Observable_EntityId>();
	for (IndexT i = 0; i < ids.Size(); i++)
	{
		if (entityType == Model)
		{
			const Util::Array<Models::ModelNode::Instance*>& nodes = Models::ModelContext::GetModelNodeInstances(id);

			for (IndexT j = 0; j < nodes.Size(); j++)
			{
				Models::ModelNode::Instance* node = nodes[j];
				if (node->node->GetType() >= Models::NodeHasShaderState)
				{
					Ids::Id32 res = observerAllocator.Get<Observer_ResultAllocator>(cid.id).Alloc();
					observerAllocator.Get<Observer_ResultAllocator>(cid.id).Get<VisibilityResult_Flag>(res) = Math::ClipStatus::Inside;
				}
			}
		}
		else
		{
			Ids::Id32 res = observerAllocator.Get<Observer_ResultAllocator>(cid.id).Alloc();
			observerAllocator.Get<Observer_ResultAllocator>(cid.id).Get<VisibilityResult_Flag>(res) = Math::ClipStatus::Inside;

		}
	}
}

//------------------------------------------------------------------------------
/**
*/
void 
ObserverContext::MakeDependency(const Graphics::GraphicsEntityId a, const Graphics::GraphicsEntityId b, const DependencyMode mode)
{
	const Graphics::ContextEntityId cid = GetContextId(b);
	observerAllocator.Get<Observer_Dependency>(cid.id) = a;
	observerAllocator.Get<Observer_DependencyMode>(cid.id) = mode;
}

//------------------------------------------------------------------------------
/**
*/
void 
ObserverContext::OnBeforeFrame(const Graphics::FrameContext& ctx)
{
	N_SCOPE(ObserverPrepareFrame, Visibility)
	const Util::Array<VisibilityEntityType>& observerTypes = observerAllocator.GetArray<Observer_EntityType>();
	const Util::Array<VisibilityEntityType>& observableTypes = ObservableContext::observableAllocator.GetArray<Observable_EntityType>();

	const Util::Array<Graphics::GraphicsEntityId>& observerIds = observerAllocator.GetArray<Observer_EntityId>();
	const Util::Array<Graphics::GraphicsEntityId>& observableIds = ObservableContext::observableAllocator.GetArray<Observable_EntityId>();

	Util::Array<Math::matrix44>& observerTransforms = observerAllocator.GetArray<Observer_Matrix>();

	Util::Array<Math::matrix44>& observableAtomTransforms = ObservableContext::observableAtomAllocator.GetArray<ObservableAtom_Transform>();
	Util::Array<Graphics::ContextEntityId>& observableAtomContexts = ObservableContext::observableAtomAllocator.GetArray<ObservableAtom_ContextEntity>();

	const Util::Array<VisibilityResultAllocator>& results = observerAllocator.GetArray<Observer_ResultAllocator>();
	Util::Array<Math::ClipStatus::Type*> observerResults = observerAllocator.GetArray<Observer_Results>();

	// go through all transforms and update
	IndexT i;
	for (i = 0; i < observableAtomContexts.Size(); i++)
	{
		const VisibilityEntityType type = observableTypes[observableAtomContexts[i].id];
		const Graphics::GraphicsEntityId id = observableIds[observableAtomContexts[i].id];

		switch (type)
		{
		case Model:
		{
			Models::ShaderStateNode::Instance* sinst = reinterpret_cast<Models::ShaderStateNode::Instance*>(ObservableContext::observableAtomAllocator.Get<ObservableAtom_Node>(i));
			observableAtomTransforms[i] = sinst->boundingBox.to_matrix44();
			break;
		}
		case Particle:
			observableAtomTransforms[i] = Particles::ParticleContext::GetBoundingBox(id).to_matrix44();
			break;
		case Light:
			observableAtomTransforms[i] = Lighting::LightContext::GetTransform(id);
			break;
		case LightProbe:
			observableAtomTransforms[i] = Graphics::LightProbeContext::GetTransform(id);
			break;
		}
	}

	for (i = 0; i < observerIds.Size(); i++)
	{
		const Graphics::GraphicsEntityId id = observerIds[i];
		const VisibilityEntityType type = observerTypes[i];

		VisibilityResultAllocator& result = results[i];

		// fetch current context ids
		IndexT j;
		for (j = 0; j < observableIds.Size(); j++)
		{
			const VisibilityEntityType type = observableTypes[j];
			Util::Array<Graphics::ContextEntityId>& contextIds = result.GetArray<VisibilityResult_CtxId>();

			switch (type)
			{
			case Model:
				contextIds[j] = Models::ModelContext::GetContextId(observableIds[j]);
				break;
			case Light:
				contextIds[j] = Lighting::LightContext::GetContextId(observableIds[j]);
				break;
			case LightProbe:
				contextIds[j] = Graphics::LightProbeContext::GetContextId(observableIds[j]);
				break;
			}
		}

		switch (type)
		{
		case Camera:
			observerTransforms[i] = Graphics::CameraContext::GetViewProjection(id);
			break;
		case Light:
			observerTransforms[i] = Lighting::LightContext::GetObserverTransform(id);
			break;
		case LightProbe:
			observerTransforms[i] = Graphics::LightProbeContext::GetTransform(id);
			break;
		}
	}

	// first step, go through list of visible entities and reset
	Util::Array<VisibilityResultAllocator>& vis = observerAllocator.GetArray<Observer_ResultAllocator>();

	// reset all lists to that all entities are visible
	for (i = 0; i < vis.Size(); i++)
	{
		Util::Array<Math::ClipStatus::Type>& flags = vis[i].GetArray<VisibilityResult_Flag>();
		observerResults[i] = flags.Begin();

		for (IndexT j = 0; j < flags.Size(); j++)
		{
			flags[j] = Math::ClipStatus::Outside;
		}
	}

	// prepare visibility systems
	if (observerTransforms.Size() > 0) for (i = 0; i < ObserverContext::systems.Size(); i++)
	{
		VisibilitySystem* sys = ObserverContext::systems[i];
		sys->PrepareObservers(observerTransforms.Begin(), observerResults.Begin(), observerTransforms.Size());
	}

	// setup observerable entities
	const Util::Array<Graphics::GraphicsEntityId>& ids = ObservableContext::observableAllocator.GetArray<Observable_EntityId>();
	if (observableAtomTransforms.Size() > 0) for (i = 0; i < ObserverContext::systems.Size(); i++)
	{
		VisibilitySystem* sys = ObserverContext::systems[i];
		sys->PrepareEntities(observableAtomTransforms.Begin(), ids.Begin(), observableAtomTransforms.Size());
	}

	// run all visibility systems
	IndexT j;
	if ((observerTransforms.Size() > 0) && (observableAtomTransforms.Size() > 0))
		for (j = 0; j < ObserverContext::systems.Size(); j++)
		{
			VisibilitySystem* sys = ObserverContext::systems[j];
			sys->Run();
		}

	// put a sync point for the jobs so all results are done when doing the sorting
	Jobs::JobSyncSignal(ObserverContext::jobInternalSync, ObserverContext::jobPort);
	Jobs::JobSyncThreadWait(ObserverContext::jobInternalSync, ObserverContext::jobPort);

	// handle dependencies
	bool dependencyNeeded = false;
	for (i = 0; i < vis.Size(); i++)
	{
		const Util::Array<Math::ClipStatus::Type>& flags = vis[i].GetArray<VisibilityResult_Flag>();
		Graphics::GraphicsEntityId& dependency = observerAllocator.Get<Observer_Dependency>(i);

		// run dependency resolve job
		if (dependency != Graphics::GraphicsEntityId::Invalid())
		{
			Jobs::JobContext ctx;
			ctx.uniform.scratchSize = 0;
			ctx.uniform.numBuffers = 2;
			ctx.input.numBuffers = 1;
			ctx.output.numBuffers = 1;

			const Graphics::ContextEntityId& ctxId = GetContextIdRef(dependency);

			ctx.uniform.data[0] = &observerAllocator.Get<Observer_DependencyMode>(i);
			ctx.uniform.dataSize[0] = sizeof(DependencyMode);
			ctx.uniform.data[1] = &ctxId;
			ctx.uniform.dataSize[1] = sizeof(uint32);

			const Util::Array<Math::ClipStatus::Type>& otherFlags = vis[ctxId.id].GetArray<VisibilityResult_Flag>();

			ctx.input.data[0] = otherFlags.Begin();
			ctx.input.dataSize[0] = sizeof(Math::ClipStatus::Type) * otherFlags.Size();
			ctx.input.sliceSize[0] = sizeof(Math::ClipStatus::Type) * otherFlags.Size();

			ctx.output.data[0] = flags.Begin();
			ctx.output.dataSize[0] = sizeof(Math::ClipStatus::Type) * flags.Size();
			ctx.output.sliceSize[0] = sizeof(Math::ClipStatus::Type) * flags.Size();

			// schedule job
			Jobs::JobId job = Jobs::CreateJob({ VisibilityDependencyJob });
			Jobs::JobSchedule(job, ObserverContext::jobPort, ctx, false);

			// add to delete list
			ObserverContext::runningJobs.Enqueue(job);
			dependencyNeeded = true;
		}
	}

	// again, put sync if we needed to resolve dependency
	if (dependencyNeeded)
	{
		Jobs::JobSyncSignal(ObserverContext::jobInternalSync2, ObserverContext::jobPort);
		Jobs::JobSyncThreadWait(ObserverContext::jobInternalSync2, ObserverContext::jobPort);
	}

	for (i = 0; i < vis.Size(); i++)
	{
		const Util::Array<Models::ModelNode::Instance*>& nodes = ObservableContext::observableAtomAllocator.GetArray<ObservableAtom_Node>();

		// early abort empty visibility queries
		if (nodes.Size() == 0)
		{
			continue;
		}

		const Util::Array<Math::ClipStatus::Type>& flags = vis[i].GetArray<VisibilityResult_Flag>();
		VisibilityDrawList& visibilities = observerAllocator.Get<Observer_DrawList>(i);
		Memory::ArenaAllocator<1024>& allocator = observerAllocator.Get<Observer_DrawListAllocator>(i);        

		// then execute sort job, which only runs the function once
		Jobs::JobContext ctx;
		ctx.uniform.scratchSize = 0;
		ctx.uniform.numBuffers = 1;
		ctx.input.numBuffers = 2;
		ctx.output.numBuffers = 1;

		ctx.input.data[0] = flags.Begin();
		ctx.input.dataSize[0] = sizeof(Math::ClipStatus::Type) * flags.Size();
		ctx.input.sliceSize[0] = sizeof(Math::ClipStatus::Type) * flags.Size();
		
		ctx.input.data[1] = nodes.Begin();
		ctx.input.dataSize[1] = sizeof(Models::ModelNode::Instance*) * nodes.Size();
		ctx.input.sliceSize[1] = sizeof(Models::ModelNode::Instance*) * nodes.Size();

		ctx.output.data[0] = &visibilities;
		ctx.output.dataSize[0] = sizeof(VisibilityDrawList);
		ctx.output.sliceSize[0] = sizeof(VisibilityDrawList);

		ctx.uniform.data[0] = &allocator;
		ctx.uniform.dataSize[0] = sizeof(allocator);

		// schedule job
		Jobs::JobId job = Jobs::CreateJob({ VisibilitySortJob });
		Jobs::JobSchedule(job, ObserverContext::jobPort, ctx, false);

		// add to delete list
		ObserverContext::runningJobs.Enqueue(job);
	}

	// insert sync after all visibility systems are done
	Jobs::JobSyncSignal(ObserverContext::jobHostSync, ObserverContext::jobPort);
}

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::Create()
{
	_CreateContext();
    
	__bundle.OnBeforeFrame = ObserverContext::OnBeforeFrame;
	__bundle.OnWaitForWork = ObserverContext::WaitForVisibility;
	__bundle.StageBits = &ObservableContext::__state.currentStage;
#ifndef PUBLIC_BUILD
	__bundle.OnRenderDebug = ObserverContext::OnRenderDebug;
#endif 

	ObserverContext::__state.allowedRemoveStages = Graphics::OnBeforeFrameStage;
	Graphics::GraphicsServer::Instance()->RegisterGraphicsContext(&__bundle, &__state);

	Jobs::CreateJobPortInfo info =
	{
		"VisibilityJobPort",
		1,
		System::Cpu::Core1 | System::Cpu::Core2 | System::Cpu::Core3 | System::Cpu::Core4,
		UINT_MAX
	};
	ObserverContext::jobPort = Graphics::GraphicsServer::renderSystemsJobPort;

	Jobs::CreateJobSyncInfo sinfo =
	{
		nullptr
	};
	ObserverContext::jobInternalSync = Jobs::CreateJobSync(sinfo);
	ObserverContext::jobInternalSync2 = Jobs::CreateJobSync(sinfo);
	ObserverContext::jobHostSync = Jobs::CreateJobSync(sinfo);

	_CreateContext();
}

//------------------------------------------------------------------------------
/**
*/
void 
ObserverContext::Discard()
{
	Jobs::DestroyJobPort(ObserverContext::jobPort);
	Jobs::DestroyJobSync(ObserverContext::jobInternalSync);
	Jobs::DestroyJobSync(ObserverContext::jobHostSync);
	Graphics::GraphicsServer::Instance()->UnregisterGraphicsContext(&__bundle);
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreateBoxSystem(const BoxSystemLoadInfo& info)
{
	BoxSystem* system = n_new(BoxSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreatePortalSystem(const PortalSystemLoadInfo& info)
{
	PortalSystem* system = n_new(PortalSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreateOctreeSystem(const OctreeSystemLoadInfo& info)
{
	OctreeSystem* system = n_new(OctreeSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreateQuadtreeSystem(const QuadtreeSystemLoadInfo & info)
{
	QuadtreeSystem* system = n_new(QuadtreeSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem* 
ObserverContext::CreateBruteforceSystem(const BruteforceSystemLoadInfo& info)
{
	BruteforceSystem* system = n_new(BruteforceSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::WaitForVisibility(const Graphics::FrameContext& ctx)
{
	if (ObserverContext::runningJobs.Size() > 0)
	{
		// wait for all jobs to finish
		Jobs::JobSyncHostWait(ObserverContext::jobHostSync); 

		// destroy jobs
		while (!ObserverContext::runningJobs.IsEmpty())
			Jobs::DestroyJob(ObserverContext::runningJobs.Dequeue());
	}
}

#ifndef PUBLIC_BUILD
//------------------------------------------------------------------------------
/**
*/
void 
ObserverContext::OnRenderDebug(uint32_t flags)
{
	// wait for all jobs to finish
	Jobs::JobSyncHostWait(ObserverContext::jobHostSync);

	Util::Array<VisibilityResultAllocator>& vis = observerAllocator.GetArray<3>();
	Util::FixedArray<SizeT> insideCounters(vis.Size(), 0);
	Util::FixedArray<SizeT> clippedCounters(vis.Size(), 0);
	Util::FixedArray<SizeT> totalCounters(vis.Size(), 0);
	for (IndexT i = 0; i < vis.Size(); i++)
	{
		auto res = vis[i].GetArray<VisibilityResult_Flag>();
		for (IndexT j = 0; j < res.Size(); j++)
			switch (res[j])
			{
			case Math::ClipStatus::Inside:
				insideCounters[i]++;
				totalCounters[i]++;
				break;
			case Math::ClipStatus::Clipped:
				clippedCounters[i]++;
				totalCounters[i]++;
				break;
			default:
				break;
			}
	}
	if (ImGui::Begin("Visibility", nullptr, 0))
	{
		for (IndexT i = 0; i < vis.Size(); i++)
		{
			ImGui::Text("Entities visible for observer %d: %d (inside [%d], clipped [%d])", i, totalCounters[i], insideCounters[i], clippedCounters[i]);
		}
		ImGui::End();
	}	
}
#endif

//------------------------------------------------------------------------------
/**
*/
const ObserverContext::VisibilityDrawList*
ObserverContext::GetVisibilityDrawList(const Graphics::GraphicsEntityId id)
{
	const Graphics::ContextEntityId cid = ObserverContext::GetContextId(id);
	if (cid == Graphics::ContextEntityId::Invalid())
		return nullptr;
	else 
		return &observerAllocator.Get<Observer_DrawList>(cid.id);
}

//------------------------------------------------------------------------------
/**
*/
Graphics::ContextEntityId
ObserverContext::Alloc()
{
	return observerAllocator.Alloc();
}

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::Dealloc(Graphics::ContextEntityId id)
{
	Util::Array<VisibilityDrawList>& draws = observerAllocator.GetArray<Observer_DrawList>();

	// reset all lists to that all entities are visible
	IndexT i;
	for (i = 0; i < draws.Size(); i++)
	{
		// clear draw lists
		VisibilityDrawList& draw = draws[i];
		auto it1 = draw.Begin();
		while (it1 != draw.End())
		{
			auto it2 = it1.val->Begin();
			while (it2 != it1.val->End())
			{
				it2.val->Clear();
				it2++;
			}
			it1.val->Clear();
			it1++;
		}
	}
	observerAllocator.Dealloc(id.id);
}

_ImplementContext(ObservableContext, ObservableContext::observableAllocator);

//------------------------------------------------------------------------------
/**
*/
void
ObservableContext::Setup(const Graphics::GraphicsEntityId id, VisibilityEntityType entityType)
{
	const Graphics::ContextEntityId cid = ObservableContext::GetContextId(id);
	observableAllocator.Get<Observable_EntityId>(cid.id) = id;
	observableAllocator.Get<Observable_EntityType>(cid.id) = entityType;

	// go through observers and allocate visibility slot for this object
	const Util::Array<ObserverContext::VisibilityResultAllocator>& visAllocators = ObserverContext::observerAllocator.GetArray<Observer_ResultAllocator>();
	Graphics::ContextEntityId cid2;
	switch (entityType)
	{
	case Model:
		cid2 = Models::ModelContext::GetContextId(id);
		break;
	case Light:
		cid2 = Lighting::LightContext::GetContextId(id);
		break;
	case LightProbe:
		cid2 = Graphics::LightProbeContext::GetContextId(id);
		break;
	}

	if (entityType == Model)
	{
		// get nodes
		const Util::Array<Models::ModelNode::Instance*>& nodes = Models::ModelContext::GetModelNodeInstances(id);

		// for all visibility allocator, allocate a slice for each node
		for (IndexT i = 0; i < visAllocators.Size(); i++)
		{
			ObserverContext::VisibilityResultAllocator& alloc = visAllocators[i];

			// go through model nodes and allocate a visibility flag result for each
			for (IndexT j = 0; j < nodes.Size(); j++)
			{
				Models::ModelNode::Instance* node = nodes[j];
				if (node->node->GetType() >= Models::NodeHasShaderState)
				{
					// allocate visibility result instance
					Ids::Id32 obj = alloc.Alloc();
					alloc.Get<VisibilityResult_Flag>(obj) = Math::ClipStatus::Inside;
				}
			}
		}

		// now produce as many atoms as we have visibility results, since they should match 1-1, but does not need to be copied for the results
		for (IndexT j = 0; j < nodes.Size(); j++)
		{
			Models::ModelNode::Instance* node = nodes[j];
			if (node->node->GetType() >= Models::NodeHasShaderState)
			{
				Ids::Id32 obj = ObservableContext::observableAtomAllocator.Alloc();
				ObservableContext::observableAtomAllocator.Get<ObservableAtom_ContextEntity>(obj) = cid;
				ObservableContext::observableAtomAllocator.Get<ObservableAtom_Node>(obj) = nodes[j];

				// append id to observable so we can track it, this id should also directly correspond to the VisibilityResult_Flags (above alloc.Alloc()) in all observers
				observableAllocator.Get<Observable_Atoms>(cid.id).Append(obj);
			}
		}
	}
}

//------------------------------------------------------------------------------
/**
*/
void 
ObservableContext::Create()
{
	_CreateContext();
    ObservableContext::__state.OnInstanceMoved = ObservableContext::OnInstanceMoved;
	ObservableContext::__state.allowedRemoveStages = Graphics::OnBeforeFrameStage;
    Graphics::GraphicsServer::Instance()->RegisterGraphicsContext(&ObservableContext::__bundle, &ObservableContext::__state);
}

//------------------------------------------------------------------------------
/**
*/
Graphics::ContextEntityId 
ObservableContext::Alloc()
{
	return observableAllocator.Alloc();
}

//------------------------------------------------------------------------------
/**
*/
void 
ObservableContext::Dealloc(Graphics::ContextEntityId id)
{
	// find atoms and dealloc
	Util::ArrayStack<Ids::Id32, 1>& atoms = observableAllocator.Get<Observable_Atoms>(id.id);
	const Util::Array<ObserverContext::VisibilityResultAllocator>& visAllocators = ObserverContext::observerAllocator.GetArray<Observer_ResultAllocator>();

	// cleanup visibility allocator first
	for (IndexT i = 0; i < visAllocators.Size(); i++)
	{
		ObserverContext::VisibilityResultAllocator& alloc = visAllocators[i];
		for (IndexT j = 0; j < atoms.Size(); j++)
		{
			alloc.Dealloc(atoms[j]);
		}
	}

	// now clean up all atoms
	for (IndexT i = 0; i < atoms.Size(); i++)
	{
		observableAtomAllocator.Dealloc(atoms[i]);
	}

	observableAllocator.Dealloc(id.id);
}

//------------------------------------------------------------------------------
/**
*/
void
ObservableContext::OnInstanceMoved(uint32_t toIndex, uint32_t fromIndex)
{
    //n_assert2(fromIndex >= observableAllocator.Size(), "Instance is assumed to be erased but wasn't!\n");
    auto size = observableAllocator.Size();

	// get atoms we are moving to
	Util::ArrayStack<Ids::Id32, 1>& toAtoms = observableAllocator.Get<Observable_Atoms>(toIndex);

	// first, decrement all entities above our current entity
	for (uint32_t i = toAtoms.Back(); i < observableAtomAllocator.Size(); i++)
	{
		observableAtomAllocator.Get<ObservableAtom_ContextEntity>(i).id--;
	}

	// then erase all atoms in the list, they should appear in order
	observableAtomAllocator.EraseRange(toAtoms.Front(), toAtoms.Back());

	// go through observers and deallocate visibility slot for this object
	const Util::Array<ObserverContext::VisibilityResultAllocator>& visAllocators = ObserverContext::observerAllocator.GetArray<Observer_ResultAllocator>();
	for (IndexT i = 0; i < visAllocators.Size(); i++)
	{
		ObserverContext::VisibilityResultAllocator& alloc = visAllocators[i];
		alloc.EraseRange(toAtoms.Front(), toAtoms.Back());
	}

	// when atoms are removed, shift all atom indices above where we removed down by the size
	for (uint32_t i = 0; i < observableAllocator.Size(); i++)
	{
		Util::ArrayStack<Ids::Id32, 1>& moveAtoms = observableAllocator.Get<Observable_Atoms>(i);
		if (i == toIndex)
			continue;
		for (IndexT j = 0; j < moveAtoms.Size(); j++)
			if (moveAtoms[j] >= toAtoms.Front())
				moveAtoms[j] -= toAtoms.Size();
	}
}

//------------------------------------------------------------------------------
/**
*/
void
ObservableContext::UpdateModelContextId(Graphics::GraphicsEntityId id, Graphics::ContextEntityId modelCid)
{
    auto cid = GetContextId(id);
    const Util::Array<ObserverContext::VisibilityResultAllocator>& visAllocators = ObserverContext::observerAllocator.GetArray<Observer_ResultAllocator>();
    for (IndexT i = 0; i < visAllocators.Size(); i++)
    {
        ObserverContext::VisibilityResultAllocator& alloc = visAllocators[i];
        alloc.Get<VisibilityResult_CtxId>(cid.id) = modelCid;
    }
}

} // namespace Visibility
