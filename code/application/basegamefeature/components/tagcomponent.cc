//------------------------------------------------------------------------------
//  tagcomponent.cc
//  (C) 2018-2020 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "application/stdneb.h"
#include "tagcomponent.h"
#include "basegamefeature/managers/componentmanager.h"

namespace Game
{

static TagComponentAllocator* data;

__ImplementComponent(TagComponent, data);

//------------------------------------------------------------------------------
/**
*/
void
TagComponent::Create()
{
	if (data != nullptr)
	{
		data->DestroyAll();
	}
	else
	{
        data = n_new(TagComponentAllocator);
	}

	__SetupDefaultComponentBundle(data);
	Game::ComponentManager::Instance()->RegisterComponent(data, "TagComponent"_atm, 'tagc');
}

//------------------------------------------------------------------------------
/**
*/
void
TagComponent::Discard()
{
	// Empty
}

} // namespace Game