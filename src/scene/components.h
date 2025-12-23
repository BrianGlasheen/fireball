#pragma once

#include "asset/model_manager.h"
#include "util/math.h"

#include <string>

struct Transform_Component {
	vec3 position = vec3(0.0f);
	quat orientation = quat(1.0f, 0.0f, 0.0f, 0.0f);
	vec3 scale = vec3(1.0f);
	mat4 world_transform = mat4(1.0f);
	bool dirty = false;
};

struct Name_Component {
	std::string string;
};

struct Physics_Component {
	
};

struct Model_Component {
	Model_Handle handle;
};

struct Light_Component {

};

struct Lifetime_Component {

};
